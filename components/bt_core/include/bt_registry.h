/**
 * Get the current Bluetooth implementation
 */
bt_interface_t* bt_get_implementation(void);

/**
 * Register a Bluetooth implementation
 */
void bt_register_implementation(bt_interface_t* implementation);

/**
 * Set to use mock implementation
 */
void bt_use_mock_implementation(void);

/**
 * Set to use real implementation
 */
void bt_use_real_implementation(void);
