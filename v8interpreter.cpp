#include "v8interpreter.h"
#include <thread>
#include <sys/time.h>
#ifdef USE_REPL
#include <readline/readline.h>
#include <readline/history.h>
#endif

#ifdef __GNUG__
#include <cstdlib>
#include <memory>
#include <cxxabi.h>

std::string demangle(const char* name) {

	if(strcmp(name, typeid(std::string).name()) == 0)
		return std::string("std::string");

	int status = -4; // some arbitrary value to eliminate the compiler warning

	// enable c++11 by passing the flag -std=c++11 to g++
	std::unique_ptr<char, void(*)(void*)> res {
		abi::__cxa_demangle(name, NULL, NULL, &status),
		std::free
	};

	return (status==0) ? res.get() : name ;
}

#else

// does nothing if not g++
std::string demangle(const char* name) {
	return name;
}

#endif
/*void V8Interpreter::V8ObjectBase::addToTemplate(v8::Isolate *isolate, v8::Local<v8::ObjectTemplate> otempl) {
	using namespace v8;
	Local<ObjectTemplate> jo = ObjectTemplate::New(isolate);
	otempl->Set(String::NewFromUtf8(isolate, name.c_str()), jo);

	for(const auto &f : funcList) {
		Local<Value> data = External::New(isolate, f.caller);
		Local<FunctionTemplate> ft = FunctionTemplate::New(isolate, callback, data);
		LOGD("Regestring %s in %s/%p", f.name, name, this);
		jo->Set(String::NewFromUtf8(isolate, f.name.c_str()), ft);
	}
}

void V8Interpreter::V8ObjectBase::callback(const v8::FunctionCallbackInfo<v8::Value> &v) {
	using namespace v8;
	void *ex = External::Cast(*v.Data())->Value();
	auto *f = static_cast<V8FunctionCaller*>(ex);
	f->call(v);
}
*/
class ArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
 public:
  virtual void* Allocate(size_t length) {
	void* data = AllocateUninitialized(length);
	return data == NULL ? data : memset(data, 0, length);
  }
  virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
  virtual void Free(void* data, size_t) { free(data); }
};

static ArrayBufferAllocator allocator;

class MyPlatform : public v8::Platform {
 public:
  virtual void CallOnBackgroundThread(v8::Task* task, ExpectedRuntime expected_runtime) {
  	std::thread bg([=]() {
  		task->Run();
  	});
  	bg.detach();
  
  }
  
  struct Task {
  	Task(v8::Task *task, double when) : task(task), when(when) {}
  	v8::Task *task;
  	double when;
  };
  
  std::vector<Task> tasks;

  virtual void CallOnForegroundThread(v8::Isolate* isolate, v8::Task* task) {
  	tasks.emplace_back(task, 0);
  }
  virtual void CallDelayedOnForegroundThread(v8::Isolate* isolate, v8::Task* task, double delay) {
  	tasks.emplace_back(task, delay + MonotonicallyIncreasingTime());
  }
  
  void update() {
  	auto t = MonotonicallyIncreasingTime();
  	auto it = tasks.begin();
  	while(it != tasks.end()) {
  		if(it->when >= t) {
  			it->task->Run();
  			it = tasks.erase(it);	
  		} else
  			it++;
  	}
  }

  virtual double MonotonicallyIncreasingTime() {
	timeval tv;
	gettimeofday(&tv, NULL);
	return  (double)(tv.tv_sec * 1000000 + tv.tv_usec) / 1000000.0;
  	
  }
};


V8Interpreter::V8Interpreter() {
	using namespace v8;
	if(!platform) {
		V8::InitializeICU();
		V8::InitializeExternalStartupData("");
		platform = new MyPlatform();
		V8::InitializePlatform(platform);
		V8::Initialize();
	}

	Isolate::CreateParams create_params;
	create_params.array_buffer_allocator = &allocator;
	isolate = Isolate::New(create_params);

	Isolate::Scope isolate_scope(isolate);
	HandleScope  hs(isolate);

	Local<ObjectTemplate> ot = ObjectTemplate::New(isolate);
	global_templ.Reset(isolate, ot);
};

std::string V8Interpreter::exec(const std::string &source) {
	using namespace v8;
	Isolate::Scope isolate_scope(isolate);
	HandleScope hs(isolate);
	auto c = Local<Context>::New(isolate, context);
	Context::Scope context_scope(c);

	auto fn = String::NewFromUtf8(isolate, source.c_str());

	// Compile the source code.
	Local<Script> script = Script::Compile(fn);

	// Run the script to get the result.
	Local<Value> result = script->Run();

	// Convert the result to an UTF8 string and print it.
	String::Utf8Value utf8(result);
	if(*utf8)
		return *utf8;
	return "";

}


void V8Interpreter::callWithContext(std::function<void()> cb) {
	using namespace v8;
	Isolate::Scope isolate_scope(isolate);
	HandleScope hs(isolate);
	auto c = Local<Context>::New(isolate, context);
	Context::Scope context_scope(c);
	cb();
}
void V8Interpreter::load(const std::string &fileName) {
	using namespace v8;
	Isolate::Scope isolate_scope(isolate);
	HandleScope hs(isolate);
	auto c = Local<Context>::New(isolate, context);
	Context::Scope context_scope(c);

	//utils::File f { fileName };
	//auto source_code = f.read();
	FILE *fp = fopen( fileName.c_str(), "rb");
	fseek(fp, 0, SEEK_END);
	uint32_t size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	auto source_code = new char [size + 1];
	source_code[size] = 0;
	fread(source_code, 1, size, fp);
	fclose(fp);

	auto fn = String::NewFromUtf8(isolate, source_code);

	// Create a string containing the JavaScript source code.
	//Local<String> source = String::NewFromUtf8(isolate, "Window.open(100, 100)");

	// Compile the source code.
	Local<Script> script = Script::Compile(fn);

	// Run the script to get the result.
	Local<Value> result = script->Run();

	// Convert the result to an UTF8 string and print it.
	String::Utf8Value utf8(result);
	printf("%s\n", *utf8);

}

void V8Interpreter::start() {
	using namespace v8;
	Isolate::Scope isolate_scope(isolate);
	HandleScope  hs(isolate);

	Local<ObjectTemplate> got = Local<ObjectTemplate>::New(isolate, global_templ);
	auto c = Context::New(isolate, nullptr, got);
	context.Reset(isolate, c);

}

void V8Interpreter::callback(const v8::FunctionCallbackInfo<v8::Value> &v) {
	using namespace v8;
	void *ex = External::Cast(*v.Data())->Value();
	auto *f = static_cast<V8FunctionCaller*>(ex);
	f->call(v);
};

void V8Interpreter::update() {
	((MyPlatform*)platform)->update();
}

#ifdef USE_REPL
#include <coreutils/utils.h>

std::shared_ptr<V8Interpreter::REPL> V8Interpreter::startREPL() {

	bool quit = false;

	//logging::setLevel(logging::WARNING);
	auto repl = std::make_shared<REPL>(*this);

	std::thread replThread = std::thread([=]() {
		char line[1024];

		while(!repl->doQuit) {
			char *line = readline(">");
			{ std::lock_guard<std::mutex> guard(repl->m);
				repl->haveResult = false;
				repl->lines.emplace_back(line);
			}
			while(!repl->haveResult) {
				utils::sleepms(100);
			}
			
			printf("=>%s\n", repl->result.c_str());
			
			if(line && line[0])
				add_history(line);
			if(line) free(line);
		}
	});
	replThread.detach();

	return repl;
}

#endif

v8::Platform *V8Interpreter::platform = nullptr;


#ifdef TESTME

using namespace std;

struct vec3 {
	float x;
	float y;
	float z;

	string toString() { return "Vec3"; }

};

int main() {

	V8Interpreter v8;
	v8.start();

	v8.registerFunction("print", [](string l) {
		puts(l.c_str());
	});

	v8.registerClass<vec3>()
			.field("x", &vec3::x)
			.field("y", &vec3::y)
			.field("z", &vec3::z)
			.method("toString", &vec3::toString)
			;


	v8.exec(R"(
		print("hello");
	)");

	return 0;
}

#endif