// tiny placeholder to make bt a real library when features are disabled
#ifdef CONFIG_BT_ENABLED
void __attribute__((weak)) dummy_bt_placeholder(void) {}
#endif
