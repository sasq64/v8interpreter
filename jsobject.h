#ifndef V8_INTERPRETER_JSOBJECT_H
#define V8_INTERPRETER_JSOBJECT_H

#include "v8common.h"
#include "v8cast.h"

///
/// \brief The JSObject class
/// Represents a plain unconverted javascript object in C++
///
class JSObject {
public:
	JSObject(v8::Isolate *isolate, const v8::Local<v8::Value> &v) : isolate(isolate), lv(v) {
	}

	JSObject operator[](const std::string &field) const {
		using namespace v8;
		auto s = to_js(isolate, field);
		Local<Object> o = Local<Object>::Cast(lv);
		return JSObject(isolate, o->Get(s));
	}

	JSObject operator[](const int &i) const {
		using namespace v8;
		Local<Object> o = Local<Object>::Cast(lv);
		return JSObject(isolate, o->Get(i));
	}

	bool isObject() const {
		return lv->IsObject();
	}

	bool hasKey(const std::string &key) const {
		using namespace v8;
		Local<Object> o = Local<Object>::Cast(lv);
		auto s = to_js<std::string, String>(isolate, key);
		return o->HasOwnProperty(s);
	}

	bool isString() const {
		return lv->IsString();
	}

	operator std::string() const {
		return toString();
	}

	bool isNumber() const {
		return lv->IsNumber();
	}

	double toNumber() const {
		return lv->ToNumber()->Value();
	}

	std::string toString() const {
		return to_cpp<std::string>(lv);
	}

	int toInt() const {
		return to_cpp<int>(lv);
	}

	int size() const {
		getFields();
		return fields->Length();
	}

	template <typename T> T to() {
		//LOGD("JSObject to %s", type(T));
		return to_cpp<T>(lv);
	}

	void getFields() const {
		if(fieldInit)
			return;
		auto o = v8::Local<v8::Object>::Cast(lv);
		fields = o->GetPropertyNames();
		fieldInit = true;
	}

	class const_iterator  {
	public:
		const_iterator(v8::Local<v8::Array> a, int pos = 0) : obj(a), position(pos) { }
		const_iterator(const const_iterator& rhs) : obj(rhs.obj), position(rhs.position) { }

		bool operator!= (const const_iterator& other) const {
			return position != other.position;
		}

		std::string operator* () const {
			using namespace v8;
			auto lv = obj->Get(position);
			return to_cpp<std::string>(lv);
		}

		const const_iterator& operator++ () {
			position++;
			return *this;
		}
	private:
		v8::Local<v8::Array> obj;
		int position;
	};

	const_iterator begin() const {
		getFields();
		return const_iterator(fields, 0);
	}

	const_iterator end() const {
		getFields();
		return const_iterator(fields, fields->Length());
	}
private:
	v8::Isolate *isolate;
	v8::Local<v8::Value> lv;
	mutable bool fieldInit = false;
	mutable v8::Local<v8::Array> fields;
};

// Add cast
template <> struct JSValue<JSObject> {
	static JSObject cast(const v8::Local<v8::Value> &v) {
		return JSObject(v8::Isolate::GetCurrent(), v);
	}
};

#endif // V8_INTERPRETER_JSOBJECT_H
