# v8interpreter
v8 wrapper with "smart" template based bindings

## OSX Quick test

    brew install v8
    g++ -DTESTME -std=c++11 -I/usr/local/Cellar/v8/4.5.103.35 -L/usr/local/opt/icu4c/lib -lv8_libplatform -lv8_base -lv8_libbase -lv8_snapshot -licudata -licuuc -licui18n v8interpreter.cpp -ov8test


## How it works

You can expose C++ classes, functions and fields to javascript:

```c++
struct vec3 {
    float x;
    float y;
    float z;
    string toString() { return format("Vec3: %f %f %f", x, y, z); }
};

struct Obj3D {
    vec3 pos;
    vec3 rot;
    string name;
    void setRotation(vec3 r) { rot = r; }
    vec3 getRotation() const { return r; }
};

int main() {

    V8Interpreter v8;

    v8.registerFunction("print", [](string l) {
        puts(l.c_str());
    });

    v8.registerClass<vec3>()
            .field("x", &vec3::x)
            .field("y", &vec3::y)
            .field("z", &vec3::z)
            .method("toString", &vec3::toString)
            ;

    v8.registerClass<Obj3D>()
        .field("pos", &Obj3D::pos)
        .field("rot", &Obj3D::getRotation, &Obj3D::setRotation)
        .field("name", &Obj3D::name)
        ;

    v8.registerFunction("makeObj", []() -> Obj3D* {
        return new Obj3D();
    });

}
```

```javascript

    var obj = makeObj();
    obj.pos.x = 3;
    obj.rot = { x: 1, y: 2, z: 3 };
    print(obj.rot)

```

* Fields can be exposed as pointer to class member, offset into class, or through a getter/setter combination
* Class fields are exposed by reference, fundamental types are not. That allows you to do things like `obj.pos.x = 0`
* Assigning a registered class from anonymous objects works, as long as the class has a default constructor
g++ -Os -DTESTME -Iv8build/v8/include v8interpreter.cpp -o v8 -Wl,--start-group v8build/v8/out/native/obj.target/{tools/gyp/libv8_{base,libbase,external_snapshot,libplatform},third_party/icu/libicu{uc,i18n,data}}.a -Wl,--end-group -lrt -ldl -pthread -std=c++0x
