#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

// Function declarations for Unity support functions
void unity_set_setup_function(void (*setup)(void));
void unity_set_teardown_function(void (*teardown)(void));

// Put setup and teardown function pointers in writable memory
// so we can modify them at runtime
extern void (*unity_setup_function)(void);
extern void (*unity_teardown_function)(void);

// Override Unity's setUp and tearDown to call our function pointers
#define UNITY_SET_SETUP(setup_func) unity_set_setup_function(setup_func)
#define UNITY_SET_TEARDOWN(teardown_func) unity_set_teardown_function(teardown_func)

#endif // UNITY_CONFIG_H
