#pragma once
// Force this into EVERY translation unit, including the source NimBLE host.
// Defining only sketch macros would leave the host's connection pool at three.
#include <sdkconfig.h>
#undef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#undef CONFIG_NIMBLE_MAX_CONNECTIONS
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 5
#define CONFIG_NIMBLE_MAX_CONNECTIONS 5
#undef CONFIG_BT_NIMBLE_MAX_CCCDS
#define CONFIG_BT_NIMBLE_MAX_CCCDS 20
#undef CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 8192
#undef CONFIG_NIMBLE_HOST_TASK_STACK_SIZE
#define CONFIG_NIMBLE_HOST_TASK_STACK_SIZE 8192
