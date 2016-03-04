#ifndef COREUTILS_DISPATCH_H
#define COREUTILS_DISPATCH_H

#include <functional>

#if __cplusplus <= 201200L

#ifndef CUSTOM_INDEX_SEQUENCE
#define CUSTOM_INDEX_SEQUENCE

namespace std {

// index_sequence

template <std::size_t...> struct index_sequence {};

template <std::size_t N, std::size_t... Is>
struct make_index_sequence : make_index_sequence<N - 1, N - 1, Is...> {};

template <std::size_t... Is>
struct make_index_sequence<0u, Is...> : index_sequence<Is...> { using type = index_sequence<Is...>; };

};

#endif

#endif

// struct CALLINFO {
//   CLASS* getThis();
//   T getArg(int index, T*);
//   T setReturn();
//}

// Type erased base class
template <typename CALLINFO> struct FunctionCaller {
	virtual int call(CALLINFO &ci) = 0;
};

template<typename... X> struct FunctionCallerFunctor;

// Deal with Functors/Lambdas
template <class STATE, class CALLINFO, class FX, class RET, class... ARGS> struct FunctionCallerFunctor<STATE, CALLINFO, FX, RET (FX::*)(ARGS...) const> : public FunctionCaller<CALLINFO>  {

	FunctionCallerFunctor(STATE *state, FX f) : state(state), func(f) {}

	template <size_t ... A> RET apply(CALLINFO &ci, std::index_sequence<A...>) const {
		return func(ci.getArg(A, (ARGS*)nullptr)...);
	}

	int call(CALLINFO &ci) override {
		ci.setReturn(apply(ci, std::make_index_sequence<sizeof...(ARGS)>()));
		return 0;
	}

	FX func;
	STATE *state;
};

template <class STATE, class CALLINFO, class FX, class... ARGS> struct FunctionCallerFunctor<STATE, CALLINFO, FX, void (FX::*)(ARGS...) const> : public FunctionCaller<CALLINFO>  {

	FunctionCallerFunctor(STATE *state, FX f) : state(state), func(f) {}

	template <size_t ... A> void apply(CALLINFO &ci, std::index_sequence<A...>) const {
		func(ci.getArg(A, (ARGS*)nullptr)...);
	}

	int call(CALLINFO &ci) override {
		apply(ci, std::make_index_sequence<sizeof...(ARGS)>());
		return 0;
	}

	FX func;
	STATE *state;
};

// Deal with member functions
template <class CLASS, class CALLINFO, class RET, class... ARGS> struct FunctionCallerMember : public FunctionCaller<CALLINFO> {

	FunctionCallerMember(CLASS *c, RET (CLASS::*f)(ARGS...)) : thisPtr(c), func(f) {}
	FunctionCallerMember(CLASS *c, RET (CLASS::*f)(ARGS...) const) : thisPtr(c), func2(f) {}

	template <size_t ... A> RET apply(CLASS *c, CALLINFO &ci, std::index_sequence<A...>) const {
		if(func)
			return (c->*func)(ci.getArg(A, (ARGS*)nullptr)...);
		else
			return (c->*func2)(ci.getArg(A, (ARGS*)nullptr)...);
	}

	int call(CALLINFO &ci) override {
		CLASS *c = (CLASS*)ci.getThis();
		if(!c) c = thisPtr;
		ci.setReturn(apply(c, ci, std::make_index_sequence<sizeof...(ARGS)>()));
		return 0;
	}

	RET (CLASS::*func)(ARGS...) = 0;
	RET (CLASS::*func2)(ARGS...) const = 0;
	CLASS *thisPtr;

};

// Specialization with void return value
template <class CLASS, class CALLINFO, class... ARGS> struct FunctionCallerMember<CLASS, CALLINFO, void, ARGS...> : public FunctionCaller<CALLINFO> {

	FunctionCallerMember(CLASS *c, void (CLASS::*f)(ARGS...)) : thisPtr(c), func(f) {
	}

	template <size_t ... A> void apply(CLASS *c, CALLINFO &ci, std::index_sequence<A...>) const {
		(c->*func)(ci.getArg(A, (ARGS*)nullptr)...);
	}

	int call(CALLINFO &ci) override {
		CLASS *c = (CLASS*)ci.getThis();
		if(!c) c = thisPtr;
		apply(c, ci, std::make_index_sequence<sizeof...(ARGS)>());
		return 0;
	}

	void (CLASS::*func)(ARGS...);
	CLASS *thisPtr;

};

// Deal with function pointers
template <class CALLINFO, class RET, class... ARGS> struct FunctionCallerPointer : public FunctionCaller<CALLINFO> {

	FunctionCallerPointer(RET (*f)(ARGS...)) : func(f) {}

	template <size_t ... A> RET apply(CALLINFO &ci, std::index_sequence<A...>) const {
		return func(ci.getArg(A, (ARGS*)nullptr)...);
	}

	int call(CALLINFO &ci) override {
		ci.setReturn(apply(ci, std::make_index_sequence<sizeof...(ARGS)>()));
		return 0;
	}

	RET (*func)(ARGS...);
};

// Specialization with void return value
template <class CALLINFO, class... ARGS> struct FunctionCallerPointer<CALLINFO, void, ARGS...> : public FunctionCaller<CALLINFO> {

	FunctionCallerPointer(void (*f)(ARGS...)) : func(f) {}

	template <size_t ... A> void apply(CALLINFO &ci, std::index_sequence<A...>) const {
		func(ci.getArg(A, (ARGS*)nullptr)...);
	}

	int call(CALLINFO &ci) override {
		apply(ci, std::make_index_sequence<sizeof...(ARGS)>());
		return 0;
	}

	void (*func)(ARGS...);
};

template <class CALLINFO, class RET, class... ARGS> FunctionCaller<CALLINFO>* createFunction(RET (*f)(ARGS...)) {
	return new FunctionCallerPointer<CALLINFO, RET, ARGS...>(f);
}

template <class CALLINFO, class FX> FunctionCaller<CALLINFO>* createFunction(FX f) {
	return new FunctionCallerFunctor<void, CALLINFO, FX, decltype(&FX::operator()) >(nullptr, f);
}

template <class CALLINFO, class STATE, class FX> FunctionCaller<CALLINFO>* createFunction(STATE *state, FX f) {
	return new FunctionCallerFunctor<STATE, CALLINFO, FX, decltype(&FX::operator()) >(state, f);
}

template <class CALLINFO, class CLASS, class RET, class... ARGS> FunctionCaller<CALLINFO>* createFunction(CLASS *c, RET (CLASS::*f)(ARGS...) const) {
	return new FunctionCallerMember<CLASS, CALLINFO, RET, ARGS...>(c, f);
}

template <class CALLINFO, class CLASS, class RET, class... ARGS> FunctionCaller<CALLINFO>* createFunction(CLASS *c, RET (CLASS::*f)(ARGS...)) {
	return new FunctionCallerMember<CLASS, CALLINFO, RET, ARGS...>(c, f);
}

#endif // COREUTILS_DISPATCH_H
