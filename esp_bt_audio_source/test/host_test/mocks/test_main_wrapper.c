/* Lightweight wrapper to provide a host `main` that forwards to `app_main`
 * used in some test sources. This avoids modifying test sources and keeps
 * the mapping local to host_test build.
 */
#include <stdlib.h>

/* app_main is defined in some test files (ESP-IDF style). Provide a stub
 * declaration and forward main() to it so host linkers find a main symbol.
 */
int app_main(void);

int main(void)
{
    return app_main();
}
