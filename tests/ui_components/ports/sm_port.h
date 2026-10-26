#pragma once
#include <stddef.h>
#include <stdbool.h>
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#include "sm_hsm.h"
