/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "soc.h"
#include "posix_soc.h"

#include "NRF_HW_model_top.h"
#include "NRF_HWLowL.h"

#include "bs_tracing.h"
#include "bs_symbols.h"
#include "bs_types.h"
#include "bs_utils.h"
#include "bs_rand_main.h"
#include "bs_pc_backchannel.h"
#include "bs_dump_files.h"

#include "argparse.h"
#include "time_machine.h"

#include <assert.h>
#include "os/mynewt.h"
#include <stdio.h>

#include "os/sim.h"

uint8_t inner_main_clean_up(int exit_code)
{
//	static int max_exit_code;
//
//	max_exit_code = BS_MAX(exit_code, max_exit_code);

	/*
	 * posix_soc_clean_up may not return if this is called from a SW thread,
	 * but instead it would get posix_exit() recalled again
	 * ASAP from the HW thread
	 */
	posix_soc_clean_up();

	hwll_terminate_simulation();
	nrf_hw_models_free_all();
	bs_dump_files_close_all();

	bs_clean_back_channels();

//	uint8_t bst_result = bst_delete();
//
//	if (bst_result != 0U) {
//		bs_trace_raw_time(2, "main: The TESTCASE FAILED with return "
//				     "code %u\n", bst_result);
//	}
//	return BS_MAX(bst_result, max_exit_code);
	return 0;
}

uint8_t main_clean_up_trace_wrap(void)
{
	return inner_main_clean_up(0);
}

void posix_exit(int exit_code)
{
	exit(inner_main_clean_up(exit_code));
}

uint global_device_nbr;
struct NRF_bsim_args_t *args;

int
main(int argc, char** argv)
{

#if MYNEWT_VAL(OS_SCHEDULING)
    /* Initialize OS */
    if (!g_os_started) {
        setvbuf(stdout, NULL, _IOLBF, 512);
        setvbuf(stderr, NULL, _IOLBF, 512);

        bs_trace_register_cleanup_function(main_clean_up_trace_wrap);
        bs_trace_register_time_function(tm_get_abs_time);

        nrf_hw_pre_init();
        nrfbsim_register_args();

        args = nrfbsim_argsparse(argc, argv);
        global_device_nbr = args->global_device_nbr;

        bs_read_function_names_from_Tsymbols(argv[0]);

        nrf_hw_initialize(&args->nrf_hw);
        os_init(main);
        os_start();
    }
#endif

    sysinit();

    /*
	* Let's ensure that even if we are redirecting to a file, we get stdout
	* and stderr line buffered (default for console)
	* Note that glibc ignores size. But just in case we set a reasonable
	* number in case somebody tries to compile against a different library
	*/

    bs_trace_raw(9, "%s: Connecting to phy...\n", __func__);
    hwll_connect_to_phy(args->device_nbr, args->s_id, args->p_id);
    bs_trace_raw(9, "%s: Connected\n", __func__);

    bs_random_init(args->rseed);
    bs_dump_files_open(args->s_id, args->global_device_nbr);

    /* We pass to a possible testcase its command line arguments */
	//bst_pass_args(args->test_case_argc, args->test_case_argv);

    puts("OK");

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
    return 0;
}
