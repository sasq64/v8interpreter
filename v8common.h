#ifndef V8INTERPRETER_V8_H
#define V8INTERPRETER_V8_H

#include <v8.h>
#include <v8-platform.h>
#include <string>
#include <typeinfo>
std::string demangle(const char* name);

class v8_exception : public std::exception {
public:
	v8_exception(const std::string &msg = "") : msg(msg) {}
	virtual const char *what() const throw() override { return msg.c_str(); }
private:
	std::string msg;
};

#endif // V8INTERPRETER_V8_H
