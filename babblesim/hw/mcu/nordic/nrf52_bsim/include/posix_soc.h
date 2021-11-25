/*
 * Copyright (c) 2017 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _POSIX_POSIX_SOC_INF_CLOCK_H
#define _POSIX_POSIX_SOC_INF_CLOCK_H

#include <posix_soc_if.h>
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

void posix_interrupt_raised(void);
void posix_boot_cpu(void);
int  posix_is_cpu_running(void);
void posix_change_cpu_state_and_wait(bool halted);

#ifdef __cplusplus
}
#endif

#endif /* _POSIX_POSIX_SOC_INF_CLOCK_H */
