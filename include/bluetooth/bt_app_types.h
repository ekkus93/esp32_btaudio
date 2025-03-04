#ifndef BLUETOOTH_TYPES_H
#define BLUETOOTH_TYPES_H

// Define connection states
// State tracking

typedef enum {
    APP_AV_STATE_IDLE,
    APP_AV_STATE_UNCONNECTED,      // Add this missing state
    APP_AV_STATE_DISCOVERING,      // Add this missing state
    APP_AV_STATE_CONNECTING,
    APP_AV_STATE_CONNECTED,
    APP_AV_STATE_DISCONNECTING
} app_av_state_t;

typedef enum {
    APP_AV_MEDIA_STATE_IDLE,
    APP_AV_MEDIA_STATE_STARTING,
    APP_AV_MEDIA_STATE_STARTED,
    APP_AV_MEDIA_STATE_STOPPING
} app_av_media_state_t;

#endif // BLUETOOTH_TYPES_H
