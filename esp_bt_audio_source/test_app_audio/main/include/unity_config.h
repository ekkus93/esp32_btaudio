#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

void unity_set_setup_function(void (*setup)(void));
void unity_set_teardown_function(void (*teardown)(void));

extern void (*unity_setup_function)(void);
extern void (*unity_teardown_function)(void);

#define UNITY_SET_SETUP(setup_func) unity_set_setup_function(setup_func)
#define UNITY_SET_TEARDOWN(teardown_func) unity_set_teardown_function(teardown_func)

#endif // UNITY_CONFIG_H
