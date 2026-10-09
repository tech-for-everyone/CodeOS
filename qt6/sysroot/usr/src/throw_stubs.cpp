extern "C" void abort(void);
extern "C" void *malloc(unsigned long);
extern "C" void free(void *);

namespace std {
  void __throw_bad_alloc() { abort(); }
  void __throw_bad_array_new_length() { abort(); }
  void __throw_length_error(const char*) { abort(); }
  void __throw_logic_error(const char*) { abort(); }
  void __throw_out_of_range(const char*) { abort(); }
  void __throw_out_of_range_fmt(const char*, ...) { abort(); }
  void __throw_ios_failure(const char*) { abort(); }
  void __throw_ios_failure(const char*, int) { abort(); }
  void __throw_system_error(int) { abort(); }
  void __throw_future_error(int) { abort(); }
  void __throw_runtime_error(const char*) { abort(); }
}

void *operator new(unsigned long sz) { return malloc(sz); }
void *operator new[](unsigned long sz) { return malloc(sz); }
void *operator new(unsigned long, void *p) { return p; }
void *operator new[](unsigned long, void *p) { return p; }
void operator delete(void *p) { free(p); }
void operator delete[](void *p) { free(p); }
void operator delete(void *p, unsigned long) { free(p); }
void operator delete[](void *p, unsigned long) { free(p); }
