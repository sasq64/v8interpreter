#ifndef V8INTERPRETER_H
#define V8INTERPRETER_H

#include "v8common.h"
#include "v8cast.h"
#include "dispatch.h"

#include <string>
#include <functional>
#include <vector>
#include <tuple>
#include <deque>
#include <thread>
#include <assert.h>
#include <atomic>
#include <mutex>

#define TYPE(x) demangle(typeid(x).name())

template <typename CLASS = void> CLASS * get_this(v8::Local<v8::Value> v) {

	auto obj = v8::Local<v8::Object>::Cast(v);
	if(obj->InternalFieldCount() > 0) {
		return static_cast<CLASS*>(obj->GetAlignedPointerFromInternalField(0));
	}
	return (CLASS*)nullptr;
}

template <typename CLASS, typename T> struct FieldRefBase {
	virtual void set(CLASS *p, const T &t) = 0;
	virtual T get(CLASS *p) = 0;
};

// Normal class field access
template <typename CLASS, typename T> struct FieldRef : public FieldRefBase<CLASS,T> {

	using PTM = T (CLASS::*);
	FieldRef(PTM ptm) : ptm(ptm) {}
	void set(CLASS *p, const T &t) override { p->*ptm = t; }
	T get(CLASS *p) override { return p->*ptm; }
	PTM ptm;
};


// Specialization to make pointer fields get assigned by value and not by referece
template <typename CLASS, typename T> struct FieldRef<CLASS, T*> : public FieldRefBase<CLASS,T*> {

	using PTM = T* (CLASS::*);
	FieldRef(PTM ptm) : ptm(ptm) {}
	void set(CLASS *p, T* const &t) override { *(p->*ptm) = *t; }
	T* get(CLASS *p) override { return p->*ptm; }
	PTM ptm;
};

// Class field version, returns by reference
template <typename CLASS, typename T> struct FieldPtrRef : public FieldRefBase<CLASS,T*> {

	using PTM = T (CLASS::*);
	FieldPtrRef(PTM ptm) : ptm(ptm) {}
	void set(CLASS *p, T* const &t) override { p->*ptm = *t; }
	T* get(CLASS *p) override { return &(p->*ptm); }
	PTM ptm;
};

// Accessor functions version, uses getter and setter.
template <typename CLASS, typename T> struct AccessorRef : public FieldRefBase<CLASS,T> {
	AccessorRef(T (CLASS::*g)(), void (CLASS::*s)(T)) : getter(g), setter(s) {}

	void set(CLASS *p, const T &t) override {
		(p->*setter)(t);
	}
	T get(CLASS *p) override {
		return (p->*getter)();
	}

	T (CLASS::*getter)();
	void (CLASS::*setter)(T);
};

// Access field through offset into object
template <typename CLASS, typename T> struct OffsetRef : public FieldRefBase<CLASS,T> {

	OffsetRef(int offset) : offset(offset) {}
	void set(CLASS *p, const T &t) override { *(T*)((uint8_t*)p + offset) = t; }
	T get(CLASS *p) override {
		T* pt = (T*)((uint8_t*)p + offset);
		return *pt;
	}
	int offset;
};


///
/// \brief The V8CallInfo class
///
class V8CallInfo {
public:
	V8CallInfo(const v8::FunctionCallbackInfo<v8::Value> &cbi) : cbi(cbi) {}

	template <class T> T getArg(int index, const T*) const {
		return to_cpp<T>(cbi[index]);
	}

	template <class T> T* getArg(int index, T* const*) const {
		return to_cpp<T*>(cbi[index]);
	}

	template <typename ... ARGS> std::function<void(ARGS...)> getArg(int index, std::function<void(ARGS...)>*) const {
		using namespace v8;
		auto *isolate = cbi.GetIsolate();
		auto lf = Local<Function>::Cast(cbi[index]);
		auto pf = std::make_shared<UniquePersistent<Function>>(isolate, lf);
		return [=](ARGS... args) {
			Isolate::Scope isolate_scope(isolate);
			HandleScope hs(isolate);
			auto recv = Null(isolate);
			Local<Value> arg_array[] = { to_js(isolate, args)... };
			auto f = Local<Function>::New(isolate, *pf);
			f->Call(recv, sizeof...(args), arg_array);
		};
	}

	template <typename T> void setReturn(T* v) const {
		using namespace v8;
		auto *isolate = cbi.GetIsolate();
		cbi.GetReturnValue().Set(to_js(isolate, v));
	}

	template <typename T> void setReturn(T v) const {
		using namespace v8;
		auto *isolate = cbi.GetIsolate();
		cbi.GetReturnValue().Set(to_js(isolate, v));
	}

	void setReturn(bool v) const {
		cbi.GetReturnValue().Set(v);
	}

	void setReturn(int i) const {
		cbi.GetReturnValue().Set(i);
	}

	void setThis(void *t) { thisPtr = t; }
	void *getThis() const { return thisPtr; }


private:
	void *thisPtr;
	const v8::FunctionCallbackInfo<v8::Value> &cbi;
};


//
// V8Class
//
template <typename CLASS> struct V8Class {

	V8Class(v8::Isolate *isolate, CLASS *thisPtr = nullptr) : isolate(isolate), thisPtr(thisPtr) {
		JSClass<CLASS>::regClass(isolate);
		otempl = JSClass<CLASS>::get();
		if(getClass())
			throw v8_exception(std::string("Class (") + TYPE(CLASS) + "already registered");
		getClass() = this;
	}

	// TODO: Split into private holder method and public non reference method
	static V8Class*& getClass() {
		static V8Class *ptr = nullptr;
		return ptr;
	}

	v8::Local<v8::Object> createInstance(CLASS *ptr) {
		using namespace v8;
		auto ot = Local<ObjectTemplate>::New(isolate, *otempl);
		Local<Object> obj = ot->NewInstance();
		obj->SetAlignedPointerInInternalField(0, ptr);
		return obj;
	}

	using V8FunctionCaller = FunctionCaller<const V8CallInfo>;
	template <typename T> using PTM = T (CLASS::*);

	template <class FX, class RET, class... ARGS> V8Class& _method(const std::string &name, FX fx) {
		return *this;
	}

	template <class FX> V8Class& method(const std::string &name, FX f) {
		_method<FX, decltype(&FX::operator())>(name, f);
		return *this;
	}
	
	template <class RET, class... ARGS> V8Class& method(const std::string &name, RET (CLASS::*f)(ARGS...)) {
		using namespace v8;
		HandleScope hs(isolate);

		V8FunctionCaller *fn = createFunction<const V8CallInfo>(thisPtr, f);

		Local<Value> data = External::New(isolate, fn);

		auto s = to_js<std::string, String>(isolate, name);
		auto o = Local<ObjectTemplate>::New(isolate, *otempl);

		Local<FunctionTemplate> ft = FunctionTemplate::New(isolate, callback, data);
		o->Set(s, ft);
		return *this;
	}

	template <class RET, class... ARGS> V8Class& method(const std::string &name, RET (*f)(ARGS...)) {
		using namespace v8;
		HandleScope hs(isolate);

		V8FunctionCaller *fn = createFunction<const V8CallInfo>(f);

		Local<Value> data = External::New(isolate, fn);

		auto s = to_js<std::string, String>(isolate, name);
		auto o = Local<ObjectTemplate>::New(isolate, *otempl);

		Local<FunctionTemplate> ft = FunctionTemplate::New(isolate, callback_static, data);
		o->Set(s, ft);
		return *this;
	}

	static void callback(const v8::FunctionCallbackInfo<v8::Value> &info) {
		void *p = get_this(info.This());
		if(!p)
			throw v8_exception(std::string("No `this` whe calling `") + TYPE(CLASS) + "." + to_cpp<std::string>(info.Callee()->GetName()) + "()`");

		void *ex = v8::External::Cast(*info.Data())->Value();
		auto *f = static_cast<V8FunctionCaller*>(ex);
		V8CallInfo ci(info);
		ci.setThis(p);
		f->call(ci);
	}

	static void callback_static(const v8::FunctionCallbackInfo<v8::Value> &info) {
		void *ex = v8::External::Cast(*info.Data())->Value();
		auto *f = static_cast<V8FunctionCaller*>(ex);
		V8CallInfo ci(info);
		f->call(ci);
	}


	template <typename T, typename C> void setAcessor(const std::string &name, FieldRefBase<C, T> *fr, v8::AccessorGetterCallback gcb, v8::AccessorSetterCallback scb) const {
		using namespace v8;
		HandleScope hs(isolate);
		Local<Value> data = External::New(isolate, fr);
		auto s = to_js<std::string, String>(isolate, name);
		auto o = Local<ObjectTemplate>::New(isolate, *otempl);
		o->SetAccessor(s, gcb, scb, data);
	}

	template <typename T, typename S = T> using is_class = typename std::enable_if<std::is_class<T>::value, S>::type;
	template <typename T, typename S = T> using is_not_class = typename std::enable_if<!std::is_class<T>::value, S>::type;
	
	template <typename BASE, typename DER, typename S = DER> using is_base_of = typename std::enable_if<std::is_base_of<BASE, DER>::value, S>::type;
	
	
	template <typename T, typename C> is_base_of<CLASS, C, is_class<T, V8Class&>> field(const std::string &name, T (C::*ptm)) {
		setAcessor(name, new FieldPtrRef<C, T>(ptm), get_cb<T*>, set_cb<T*>);
		return *this;
	}

	template <typename T, typename C> is_not_class<T, V8Class&> field(const std::string &name, T (C::*ptm)) {
		setAcessor(name, new FieldRef<C, T>(ptm), get_cb<T>, set_cb<T>);
		return *this;
	}

	template <typename T> V8Class& field(const std::string &name, int offset) {
		setAcessor(name, new OffsetRef<CLASS, T>(offset), get_cb<T>, set_cb<T>);
		return *this;
	}
	
	template <class RET> V8Class& field(const std::string &name, RET (CLASS::*getter)(), void (CLASS::*setter)(RET)) {
		setAcessor(name, new AccessorRef<CLASS, RET>(getter, setter), get_cb<RET>, set_cb<RET>);
		return *this;
	}
	
	template <typename T> static void get_cb(v8::Local<v8::String> s, const v8::PropertyCallbackInfo<v8::Value> &info) {
		using namespace v8;
		CLASS *p = get_this<CLASS>(info.This());
		//LOGD("Getting %s::%s = %s (%p)", TYPE(CLASS), to_cpp<std::string>(s), TYPE(T), p);
		if(!p)
			throw v8_exception(std::string("No `this` when getting field `") + TYPE(CLASS) + "." + to_cpp<std::string>(s) + "`");
		auto e = Local<External>::Cast(info.Data());
		auto *f = static_cast<FieldRefBase<CLASS, T>*>(e->Value());
		auto *isolate = info.GetIsolate();
		T t = f->get(p);
		info.GetReturnValue().Set(to_js<T>(isolate, t));
	}

	template <typename T> static void set_cb(v8::Local<v8::String> s, v8::Local<v8::Value> val, const v8::PropertyCallbackInfo<void> &info) {
		using namespace v8;
		//LOGD("Setting %s::%s = %s", TYPE(CLASS), to_cpp<std::string>(s), TYPE(T));
		CLASS *p = get_this<CLASS>(info.This());
		if(!p)
			throw v8_exception(std::string("No `this` when getting field `") + TYPE(CLASS) + "." + to_cpp<std::string>(s) + "`");
		auto e = Local<External>::Cast(info.Data());
		auto *f = static_cast<FieldRefBase<CLASS, T>*>(e->Value());
		f->set(p, to_cpp<T>(val));
	}

	v8::UniquePersistent<v8::ObjectTemplate> *getTemplate() {
		return otempl;
	}

	v8::Isolate *isolate;
	v8::UniquePersistent<v8::ObjectTemplate> *otempl;
	CLASS *thisPtr;
};


///
/// \brief The V8Interpreter class
///
class V8Interpreter {
public:

	using V8FunctionCaller = FunctionCaller<const V8CallInfo>;

	template <class FX> void registerFunction(const std::string &name, FX f) {
		using namespace v8;

		HandleScope hs(isolate);
		auto c = Local<Context>::New(isolate, context);
		Context::Scope context_scope(c);

		V8FunctionCaller *fn = createFunction<const V8CallInfo>(f);

		Local<Value> data = External::New(isolate, fn);
		Local<FunctionTemplate> ft = FunctionTemplate::New(isolate, callback, data);

		auto fun = ft->GetFunction();

		Handle<Object> v8RealGlobal = Handle<Object>::Cast(c->Global()->GetPrototype());

		v8RealGlobal->Set(to_js<std::string>(isolate, name), fun);
	}

	template <class CLASS> void addGlobalObject(const std::string &name, CLASS *ptr) {
		using namespace v8;
		HandleScope hs(isolate);

		auto c = Local<Context>::New(isolate, context);
		Context::Scope context_scope(c);

		auto obj = V8Class<CLASS>::getClass()->createInstance(ptr);
		Handle<Object> v8RealGlobal = Handle<Object>::Cast(c->Global()->GetPrototype());

		v8RealGlobal->Set(to_js<std::string>(isolate, name), obj);
	}

	template <typename CLASS> V8Class<CLASS>& registerClass(const std::string &name = "", CLASS *thisPtr = nullptr) {
		using namespace v8;
		static V8Class<CLASS> *sClass = nullptr;

		if(sClass != nullptr)
			throw v8_exception(std::string("Class ") + TYPE(name) + " already registered");

		HandleScope hs(isolate);
		auto c = Local<Context>::New(isolate, context);
		sClass = new V8Class<CLASS>(isolate, thisPtr);

		return *sClass;
	}

	struct REPL {

		REPL(V8Interpreter &v8) : v8(v8), doQuit(false), haveResult(false) {}

		void update() {
			std::lock_guard<std::mutex> guard(m);
			while(lines.size() > 0) {
				auto line = lines.front();
				result = v8.exec(line);
				haveResult = true;
				lines.pop_front();
			}
		}
		void quit() {
			doQuit = true;
		}

		std::atomic<bool> doQuit;
		std::atomic<bool> haveResult;
		V8Interpreter &v8;
		std::mutex m;
		std::deque<std::string> lines;
		std::string result;
	};

	V8Interpreter();
	void start();
	static void callback(const v8::FunctionCallbackInfo<v8::Value> &v);

	void load(const std::string &file_name);
	std::string exec(const std::string &source_code);
	void callWithContext(std::function<void()> cb);
	std::shared_ptr<REPL> startREPL();
	
	static void update();

private:
	static v8::Platform *platform;
	v8::Isolate *isolate = nullptr;
	v8::UniquePersistent<v8::Context> context;
	v8::UniquePersistent<v8::ObjectTemplate> global_templ;
};

#endif // V8INTERPRETER_H
