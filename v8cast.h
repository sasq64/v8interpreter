#ifndef V8INTERPRETER_CAST_H
#define V8INTERPRETER_CAST_H

#include "v8.h"
#define TYPE(x) demangle(typeid(x).name())

#include <unordered_map>

#ifdef USE_APONE
#include <coreutils/log.h>
#else
#define LOGW(...)
#define LOGD(...)
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


template <typename T> struct ObjectHolder {

	ObjectHolder(v8::Isolate *isolate, std::shared_ptr<T> sptr) : sptr(sptr) {
		using namespace v8;
		
		T *ptr = sptr.get();
		
		Local<ObjectTemplate> ot;
		auto *pot = JSClass<T>::get();
		if(pot) {
			ot = Local<ObjectTemplate>::New(isolate, *pot);
		} else {
			LOGW("Warning: Casting to unregistered class `%s`", TYPE(T));
			ot = ObjectTemplate::New(isolate);
			ot->SetInternalFieldCount(1);
		}

		auto o = ot->NewInstance();
		o->SetAlignedPointerInInternalField(0, ptr);

		objects()[ptr] = this;
		
		holder.Reset(isolate, o);
		holder.SetWeak(this, callback, v8::WeakCallbackType::kParameter);
		holder.MarkIndependent();
		isolate->AdjustAmountOfExternalAllocatedMemory(sizeof(T));
		LOGD("Created Instance of %s = %p", TYPE(T), ptr);
	}
	
	ObjectHolder(v8::Isolate *isolate, T *ptr) {
		using namespace v8;
		Local<ObjectTemplate> ot;
		auto *pot = JSClass<T>::get();
		if(pot) {
			ot = Local<ObjectTemplate>::New(isolate, *pot);
		} else {
			LOGW("Warning: Casting to unregistered class `%s`", TYPE(T));
			ot = ObjectTemplate::New(isolate);
			ot->SetInternalFieldCount(1);
		}

		auto o = ot->NewInstance();
		o->SetAlignedPointerInInternalField(0, ptr);

		objects()[ptr] = this;
		holder.Reset(isolate, o);
	
	}
	

	static void callback(const v8::WeakCallbackInfo<ObjectHolder<T>>& data) {
		ObjectHolder<T> *param = data.GetParameter();
		LOGD("Instance of %s = %p freed", TYPE(T), param->sptr.get());
		objects()[param->sptr.get()] = nullptr;
		param->holder.Reset();
		delete param;
	}
	
	v8::UniquePersistent<v8::Value> holder;
	std::shared_ptr<T> sptr;
	
	static v8::Local<v8::Value> get(v8::Isolate *isolate, std::shared_ptr<T> sp) {	
		auto *oh = ObjectHolder<T>::objects()[sp.get()];
		if(!oh)
			oh = new ObjectHolder(isolate, sp);
		//else
			//LOGD("Found existing js object for %s = %p", TYPE(T), sp.get());
		return v8::Local<v8::Value>::New(isolate, oh->holder);	
	}		

	static v8::Local<v8::Value> get(v8::Isolate *isolate, T *ptr) {	
		auto *oh = ObjectHolder<T>::objects()[ptr];
		if(!oh)
			oh = new ObjectHolder(isolate, ptr);
		//else
			//LOGD("Found existing js object for RAWPTR %s = %p", TYPE(T), ptr);
		return v8::Local<v8::Value>::New(isolate, oh->holder);	
	}		
	
	static std::unordered_map<T*, ObjectHolder*>& objects() {
		static std::unordered_map<T*, ObjectHolder*> _objs;
		return _objs;
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

template <typename T> struct JSValue<std::shared_ptr<T>> {
	static std::shared_ptr<T> cast(const v8::Local<v8::Value> &v) {
		auto obj = v8::Local<v8::Object>::Cast(v);
		if(obj->InternalFieldCount() > 0) {
			T *ptr = static_cast<T*>(obj->GetAlignedPointerFromInternalField(0));
			auto *oh = ObjectHolder<T>::objects()[ptr];
			if(oh)
				return oh->sptr;
		}
		return std::make_shared<T>(JSValue<T>::cast(v));
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

template <typename T, typename S = T> using is_arithmetic = typename std::enable_if<std::is_arithmetic<T>::value, S>::type;
template <typename T, typename S = T> using is_not_arithmetic = typename std::enable_if<!std::is_arithmetic<T>::value, S>::type;

template <typename T, typename V = v8::Value> struct CPPValue {
	static v8::Local<V> cast(v8::Isolate *isolate, const T &t) {
		using namespace v8;
		//LOGW("Creating copy of %s", TYPE(T));	
		return ObjectHolder<T>::get(isolate, std::make_shared<T>(t));
	}
};

template <typename T, typename V> struct CPPValue<T*, V> {

	static v8::Local<V> cast(v8::Isolate *isolate, T *t) {
		using namespace v8;
		if(!t)
			return v8::Null(isolate);
		// TODO: Option to disallow raw pointers that does not map to a previous shared_ptr
	//	LOGW("Using raw ptr of %s", TYPE(T));	
		return ObjectHolder<T>::get(isolate, t);
	}
};

template <typename T, typename V> struct CPPValue<std::shared_ptr<T>, V> {
	static v8::Local<V> cast(v8::Isolate *isolate, std::shared_ptr<T> t) {
		if(!t)
			return v8::Null(isolate);
		return ObjectHolder<T>::get(isolate, t);
	}
};

template <typename V> struct CPPValue<std::string*, V> {
	static v8::Local<V> cast(v8::Isolate *isolate, const std::string *t) {
		return v8::String::NewFromUtf8(isolate, t->c_str());
	}
};

template <typename V> struct CPPValue<std::string, V> {
	static v8::Local<V> cast(v8::Isolate *isolate, const std::string &t) {
		return v8::String::NewFromUtf8(isolate, t.c_str());
	}
};

template<typename T, typename V = v8::Value> static is_arithmetic<T, v8::Local<V>> to_js(v8::Isolate *isolate, const T &t) {
	return v8::Number::New(isolate, t);
}

template<typename T, typename V = v8::Value> static is_not_arithmetic<T, v8::Local<V>> to_js(v8::Isolate *isolate, const T &t) {
	return CPPValue<T,V>::cast(isolate, t);
}

#ifndef USE_APONE
#undef LOGW
#undef LOGD
#endif

#endif // V8INTERPRETER_CAST_H

