#ifndef V8INTERPRETER_CAST_H
#define V8INTERPRETER_CAST_H

#include "v8.h"
#define TYPE(x) demangle(typeid(x).name())


#ifndef LOGW
#define LOGW(...)
#endif

/// ****************************** TYPE CONVERSION UTILS ***********************************

template <typename CLASS> struct JSClass {

	
	using Template = v8::UniquePersistent<v8::ObjectTemplate>;

	static Template*& get() {
		static Template *ptr;
		return ptr;
	}

	static v8::Isolate*& get_isolate() {
		static v8::Isolate *i;
		return i;
	}

	static void regClass(v8::Isolate *isolate) {

		auto ot = v8::ObjectTemplate::New(isolate);
		ot->SetInternalFieldCount(1);
		auto *otempl = new Template(isolate, ot);
		get() = otempl;
		get_isolate() = isolate;
	}

	static v8::Local<v8::Object> createInstance(v8::Isolate *isolate, CLASS *ptr) {
		using namespace v8;
		auto *otempl = get();
		if(!otempl)
			throw v8_exception(std::string("Can not create unregistered class `") + TYPE(CLASS) + "`");
		auto ot = Local<ObjectTemplate>::New(isolate, *otempl);
		Local<Object> obj = ot->NewInstance();
		obj->SetAlignedPointerInInternalField(0, ptr);
		return obj;
	}
};



// Convert V8 Locals to C++ type
template <typename T> struct JSValue {
	static T cast(const v8::Local<v8::Value> &v) {
		using namespace v8;
		auto obj = v8::Local<v8::Object>::Cast(v);
		
		// If object was created on the native side, it will contain a pointer
		if(obj->InternalFieldCount() > 0) {
			T *t = static_cast<T*>(obj->GetAlignedPointerFromInternalField(0));
			return *t;
		}

		// Otherwise create a default T object, and utilize existing setters to set
		// fields from the javascript object. 
		T result;
		v8::Isolate *isolate = JSClass<T>::get_isolate();
		auto dst = JSClass<T>::createInstance(isolate, &result);
		Local<Object> src = Local<Object>::Cast(v);
		Local<Array> parray = src->GetPropertyNames();
		for(int i=0; i<parray->Length(); i++) {
			Local<Value> keyv = parray->Get(i);
			auto key = Local<String>::Cast(keyv);
			Local<Value> val = src->Get(key);
			dst->Set(key, val);
		}

		return result;
	}
};

template <> struct JSValue<double> {
	static double cast(const v8::Local<v8::Value> &v) {
		return v->ToNumber()->Value();
	}
};

template <> struct JSValue<float> {
	static float cast(const v8::Local<v8::Value> &v) {
		return v->ToNumber()->Value();
	}
};

template <> struct JSValue<int> {
	static int cast(const v8::Local<v8::Value> &v) {
		return v->ToInteger()->Value();
	}
};

template <> struct JSValue<unsigned int> {
	static unsigned int cast(const v8::Local<v8::Value> &v) {
		return v->ToUint32()->Value();
	}
};

template <typename T> struct JSValue<T*> {
	static T* cast(const v8::Local<v8::Value> &v) {
		T *t = nullptr;
		auto obj = v8::Local<v8::Object>::Cast(v);
		if(obj->InternalFieldCount() > 0) {
			t = static_cast<T*>(obj->GetAlignedPointerFromInternalField(0));
		}
		if(!t) {
			t = new T(JSValue<T>::cast(v));
		}
		return t;
	}
};

template <> struct JSValue<std::string> {
	static std::string cast(const v8::Local<v8::Value> &v) {
		char target[1024];
		v->ToString()->WriteUtf8(target, sizeof(target));
		return std::string(target);
	}
};


//// The wrapper function for the class templates
template <typename T> T to_cpp(const v8::Local<v8::Value> &v) {
	return JSValue<T>::cast(v);
}

///
// Convert C++ types to V8 local
///////////////////////////////////////////////////////////////////////////////////////
///
///
///


// Helper class that frees temporary objects attached to javascript objects during garbage collection
template <typename T> struct HandHolder {
	HandHolder(v8::Isolate *isolate, T *ptr, const v8::Local<v8::Value> &v) : ptr(ptr) {
		holder.Reset(isolate, v);
		holder.SetWeak(this, callback, v8::WeakCallbackType::kParameter);
		holder.MarkIndependent();
		isolate->AdjustAmountOfExternalAllocatedMemory(sizeof(T));
	}

	static void callback(const v8::WeakCallbackInfo<HandHolder<T>>& data) {
		HandHolder<T> *param = data.GetParameter();
		param->holder.Reset();
		delete param->ptr;
		delete param;
	}

	T *ptr;
	v8::UniquePersistent<v8::Value> holder;
};


template <typename T, typename V = v8::Value> struct CPPValue {
	static v8::Local<V> cast(v8::Isolate *isolate, const T &t) {
		using namespace v8;
		// NOTE: Relies on class registration
		Local<ObjectTemplate> ot;
		auto *pot = JSClass<T>::get();

		//LOGD("Converting REF %s to js = %p", demangle(typeid(T).name()), pot);

		if(pot) {
			ot = Local<ObjectTemplate>::New(isolate, *pot);
			//LOGD("JSClass exists!");
		} else {
			LOGW("Warning: Casting to unregistered class `%s`", TYPE(T));
			ot = ObjectTemplate::New(isolate);
			ot->SetInternalFieldCount(1);
		}

		auto o = ot->NewInstance();

		// Create a copy of the object and make sure it is freed by garbage collector
		T *ct = new T(t);
		new HandHolder<T>(isolate, ct, o);

		o->SetAlignedPointerInInternalField(0, ct);
		return o;
	}
};

template <typename T, typename V> struct CPPValue<T*, V> {
	static v8::Local<V> cast(v8::Isolate *isolate, T *t) {
		using namespace v8;
		// NOTE: Relies on class registration
		Local<ObjectTemplate> ot;
		auto *pot = JSClass<T>::get();

		//LOGD("Converting PTR %p %s to js = %p", t, demangle(typeid(T).name()), pot);

		if(pot) {
			ot = Local<ObjectTemplate>::New(isolate, *pot);
		} else {
			LOGW("Warning: Casting to unregistered class `%s`", TYPE(T));
			ot = ObjectTemplate::New(isolate);
			ot->SetInternalFieldCount(1);
		}

		auto o = ot->NewInstance();
		o->SetAlignedPointerInInternalField(0, t);
		return o;
	}
};

template <typename V> struct CPPValue<int, V> {
	static v8::Local<V> cast(v8::Isolate *isolate, const int &t) {
		return v8::Number::New(isolate, t);
	}
};

template <typename V> struct CPPValue<double, V> {
	static v8::Local<V> cast(v8::Isolate *isolate, const double &t) {
		return v8::Number::New(isolate, t);
	}
};

template <typename V> struct CPPValue<float, V> {
	static v8::Local<V> cast(v8::Isolate *isolate, const float &t) {
		return v8::Number::New(isolate, t);
	}
};

template <typename V> struct CPPValue<std::string, V> {
	static v8::Local<V> cast(v8::Isolate *isolate, const std::string &t) {
		return v8::String::NewFromUtf8(isolate, t.c_str());
	}
};

template<typename T, typename V = v8::Value> static v8::Local<V> to_js(v8::Isolate *isolate, const T &t) {
	return CPPValue<T,V>::cast(isolate, t);
}

#endif // V8INTERPRETER_CAST_H
