/*
 * Copyright 2019 International Business Machines
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * OCACCEL HelloWorld Example
 *
 * Demonstration how to get data into the FPGA, process it using a OCACCEL
 * action and move the data out of the FPGA back to host-DRAM.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>
#include <malloc.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <assert.h>

#include <ocaccel_tools.h>
#include <libocaccel.h>
#include <action_changecase.h>
#include <ocaccel_hls_if.h>

int verbose_flag = 0;

static const char *version = GIT_VERSION;

static const char *mem_tab[] = { "HOST_DRAM", "CARD_DRAM", "TYPE_NVME" };

/**
 * @brief	prints valid command line options
 *
 * @param prog	current program's name
 */
static void usage(const char *prog)
{
	printf("Usage: %s [-h] [-v, --verbose] [-V, --version]\n"
	"  -C, --card <cardno>       can be (0...3)\n"
	"  -i, --input <file.bin>    input file.\n"
	"  -o, --output <file.bin>   output file.\n"
	"  -A, --type-in <CARD_DRAM, HOST_DRAM, ...>.\n"
	"  -a, --addr-in <addr>      address e.g. in CARD_RAM.\n"
	"  -D, --type-out <CARD_DRAM,HOST_DRAM, ...>.\n"
	"  -d, --addr-out <addr>     address e.g. in CARD_RAM.\n"
	"  -s, --size <size>         size of data.\n"
	"  -t, --timeout             timeout in sec to wait for done.\n"
	"  -X, --verify              verify result if possible\n"
	"  -N, --no-irq              disable Interrupts\n"
	"\n"
	"Useful parameters (to be placed before the command):\n"
	"----------------------------------------------------\n"
	"OCACCEL_TRACE=0x0   no debug trace  (default mode)\n"
	"OCACCEL_TRACE=0xF   full debug trace\n"
	"\n"
        "Example\n"
        "------------------------\n"
        "echo Clean possible temporary old files \n"
	"echo Prepare the text to process\n"
	"echo \"Hello world. This is my first CAPI OCACCEL experience. It's real fun.\""
	" > /tmp/t1\n"
	"\n"
	"echo Run the application + hardware action\n"
	"ocaccel_helloworld -i /tmp/t1 -o /tmp/t2\n"
	"echo Display input file: && cat /tmp/t1\n"
	"echo Display output file from FPGA executed action -UPPER CASE expected-:"
	" && cat /tmp/t2\n"
	"\n",
        prog);
}

// Function that fills the MMIO registers / data structure 
// these are all data exchanged between the application and the action
static void ocaccel_prepare_helloworld(struct ocaccel_job *cjob,
				 struct helloworld_job *mjob,
				 void *addr_in,
				 uint32_t size_in,
				 uint8_t type_in,
				 void *addr_out,
				 uint32_t size_out,
				 uint8_t type_out)
{
	fprintf(stderr, "  prepare helloworld job of %ld bytes size\n", sizeof(*mjob));

	assert(sizeof(*mjob) <= OCACCEL_JOBSIZE);
	memset(mjob, 0, sizeof(*mjob));

	// Setting input params : where text is located in host memory
	ocaccel_addr_set(&mjob->in, addr_in, size_in, type_in,
		      OCACCEL_ADDRFLAG_ADDR | OCACCEL_ADDRFLAG_SRC);
	// Setting output params : where result will be written in host memory
	ocaccel_addr_set(&mjob->out, addr_out, size_out, type_out,
		      OCACCEL_ADDRFLAG_ADDR | OCACCEL_ADDRFLAG_DST |
		      OCACCEL_ADDRFLAG_END);

	ocaccel_job_set(cjob, mjob, sizeof(*mjob), NULL, 0);
}

/* main program of the application for the hls_helloworld example        */
/* This application will always be run on CPU and will call either       */
/* a software action (CPU executed) or a hardware action (FPGA executed) */
int main(int argc, char *argv[])
{
	// Init of all the default values used 
	int ch, rc = 0;
	int card_no = 0;
	struct ocaccel_card *card = NULL;
	struct ocaccel_action *action = NULL;
	char device[128];
	struct ocaccel_job cjob;
	struct helloworld_job mjob;
	const char *input = NULL;
	const char *output = NULL;
	unsigned long timeout = 600;
	const char *space = "CARD_RAM";
	struct timeval etime, stime;
	ssize_t size = 1024 * 1024;
	uint8_t *ibuff = NULL, *obuff = NULL;
	uint8_t type_in = OCACCEL_ADDRTYPE_HOST_DRAM;
	uint64_t addr_in = 0x0ull;
	uint8_t type_out = OCACCEL_ADDRTYPE_HOST_DRAM;
	uint64_t addr_out = 0x0ull;
	int verify = 0;
	int exit_code = EXIT_SUCCESS;
	uint8_t trailing_zeros[1024] = { 0, };
	// default is interrupt mode enabled (vs polling)
	//ocaccel_action_flag_t action_irq = (OCACCEL_ACTION_DONE_IRQ | OCACCEL_ATTACH_IRQ);
	ocaccel_action_flag_t action_irq = OCACCEL_ACTION_DONE_IRQ;

	// collecting the command line arguments
	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{ "card",	 required_argument, NULL, 'C' },
			{ "input",	 required_argument, NULL, 'i' },
			{ "output",	 required_argument, NULL, 'o' },
			{ "src-type",	 required_argument, NULL, 'A' },
			{ "src-addr",	 required_argument, NULL, 'a' },
			{ "dst-type",	 required_argument, NULL, 'D' },
			{ "dst-addr",	 required_argument, NULL, 'd' },
			{ "size",	 required_argument, NULL, 's' },
			{ "timeout",	 required_argument, NULL, 't' },
			{ "verify",	 no_argument,	    NULL, 'X' },
			{ "no-irq",	 no_argument,	    NULL, 'N' },
			{ "version",	 no_argument,	    NULL, 'V' },
			{ "verbose",	 no_argument,	    NULL, 'v' },
			{ "help",	 no_argument,	    NULL, 'h' },
			{ 0,		 no_argument,	    NULL, 0   },
		};

		ch = getopt_long(argc, argv,
                                 "C:i:o:A:a:D:d:s:t:XNVvh",
				 long_options, &option_index);
		if (ch == -1)
			break;

		switch (ch) {
		case 'C':
			card_no = strtol(optarg, (char **)NULL, 0);
			break;
		case 'i':
			input = optarg;
			break;
		case 'o':
			output = optarg;
			break;
			/* input data */
		case 'A':
			space = optarg;
			if (strcmp(space, "CARD_DRAM") == 0)
				type_in = OCACCEL_ADDRTYPE_CARD_DRAM;
			else if (strcmp(space, "HOST_DRAM") == 0)
				type_in = OCACCEL_ADDRTYPE_HOST_DRAM;
			else {
				usage(argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		case 'a':
			addr_in = strtol(optarg, (char **)NULL, 0);
			break;
			/* output data */
		case 'D':
			space = optarg;
			if (strcmp(space, "CARD_DRAM") == 0)
				type_out = OCACCEL_ADDRTYPE_CARD_DRAM;
			else if (strcmp(space, "HOST_DRAM") == 0)
				type_out = OCACCEL_ADDRTYPE_HOST_DRAM;
			else {
				usage(argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		case 'd':
			addr_out = strtol(optarg, (char **)NULL, 0);
			break;
                case 's':
                        size = __str_to_num(optarg);
                        break;
                case 't':
                        timeout = strtol(optarg, (char **)NULL, 0);
                        break;		
                case 'X':
			verify++;
			break;
                case 'N':
                        action_irq = 0;
                        break;
			/* service */
		case 'V':
			printf("%s\n", version);
			exit(EXIT_SUCCESS);
		case 'v':
			verbose_flag = 1;
			break;
		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;
		default:
			usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if (optind != argc) {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}
	if (argc == 1) {       // to provide help when program is called without argument
          usage(argv[0]);
          exit(EXIT_FAILURE);
        }

	/* if input file is defined, use that as input */
	if (input != NULL) {
		size = __file_size(input);
		if (size < 0)
			goto out_error;

		/* Allocate in host memory the place to put the text to process */
		ibuff = ocaccel_malloc(size); //64Bytes aligned malloc
		if (ibuff == NULL)
			goto out_error;
		memset(ibuff, 0, size);

		fprintf(stdout, "reading input data %d bytes from %s\n",
			(int)size, input);

		// copy text from file to host memory
		rc = __file_read(input, ibuff, size);
		if (rc < 0)
			goto out_error;

		// prepare params to be written in MMIO registers for action
		type_in = OCACCEL_ADDRTYPE_HOST_DRAM;
		addr_in = (unsigned long)ibuff;
	}

	/* if output file is defined, use that as output */
	if (output != NULL) {
		size_t set_size = size + (verify ? sizeof(trailing_zeros) : 0);

		/* Allocate in host memory the place to put the text processed */
		obuff = ocaccel_malloc(set_size); //64Bytes aligned malloc
		if (obuff == NULL)
			goto out_error;
		memset(obuff, 0x0, set_size);

		// prepare params to be written in MMIO registers for action
		type_out = OCACCEL_ADDRTYPE_HOST_DRAM;
		addr_out = (unsigned long)obuff;
	}


	/* Display the parameters that will be used for the example */
	printf("PARAMETERS:\n"
	       "  input:       %s\n"
	       "  output:      %s\n"
	       "  type_in:     %x %s\n"
	       "  addr_in:     %016llx\n"
	       "  type_out:    %x %s\n"
	       "  addr_out:    %016llx\n"
	       "  size_in/out: %08lx\n",
	       input  ? input  : "unknown", output ? output : "unknown",
	       type_in,  mem_tab[type_in],  (long long)addr_in,
	       type_out, mem_tab[type_out], (long long)addr_out,
	       size);


	// Allocate the card that will be used
        if(card_no == 0)
                snprintf(device, sizeof(device)-1, "IBM,oc-accel");
        else
                snprintf(device, sizeof(device)-1, "/dev/ocxl/IBM,oc-accel.000%d:00:00.1.0", card_no);

	card = ocaccel_card_alloc_dev(device, OCACCEL_VENDOR_ID_IBM,
				   OCACCEL_DEVICE_ID_OCACCEL);
	if (card == NULL) {
		fprintf(stderr, "err: failed to open card %u: %s\n",
			card_no, strerror(errno));
                fprintf(stderr, "Default mode is FPGA mode.\n");
                fprintf(stderr, "Did you want to run CPU mode ? => add OCACCEL_CONFIG=CPU before your command.\n");
                fprintf(stderr, "Otherwise make sure you ran ocaccel_find_card and ocaccel_maint for your selected card.\n");
		goto out_error;
	}

	// Attach the action that will be used on the allocated card
	action = ocaccel_attach_action(card, ACTION_TYPE, action_irq, 60);
	if(action_irq)
		ocaccel_action_assign_irq(action, ACTION_IRQ_SRC_LO);
	if (action == NULL) {
		fprintf(stderr, "err: failed to attach action %u: %s\n",
			card_no, strerror(errno));
		goto out_error1;
	}

	// Fill the stucture of data exchanged with the action
	ocaccel_prepare_helloworld(&cjob, &mjob,
			     (void *)addr_in,  size, type_in,
			     (void *)addr_out, size, type_out);

	// uncomment to dump the job structure
	//__hexdump(stderr, &mjob, sizeof(mjob));


	// Collect the timestamp BEFORE the call of the action
	gettimeofday(&stime, NULL);

	// Call the action will:
	//    write all the registers to the action (MMIO) 
	//  + start the action 
	//  + wait for completion
	//  + read all the registers from the action (MMIO) 
	rc = ocaccel_action_sync_execute_job(action, &cjob, timeout);

	// Collect the timestamp AFTER the call of the action
	gettimeofday(&etime, NULL);
	if (rc != 0) {
		fprintf(stderr, "err: job execution %d: %s!\n", rc,
			strerror(errno));
		goto out_error2;
	}

	/* If the output buffer is in host DRAM we can write it to a file */
	if (output != NULL) {
		fprintf(stdout, "writing output data %p %d bytes to %s\n",
			obuff, (int)size, output);

		rc = __file_write(output, obuff, size);
		if (rc < 0)
			goto out_error2;
	}

	// test return code
	(cjob.retc == OCACCEL_RETC_SUCCESS) ? fprintf(stdout, "SUCCESS\n") : fprintf(stdout, "FAILED\n");
	if (cjob.retc != OCACCEL_RETC_SUCCESS) {
		fprintf(stderr, "err: Unexpected RETC=%x!\n", cjob.retc);
		goto out_error2;
	}

	// Compare the input and output if verify option -X is enabled
	if (verify) {
		if ((type_in  == OCACCEL_ADDRTYPE_HOST_DRAM) &&
		    (type_out == OCACCEL_ADDRTYPE_HOST_DRAM)) {
			rc = memcmp(ibuff, obuff, size);
			if (rc != 0)
				exit_code = EX_ERR_VERIFY;

			rc = memcmp(obuff + size, trailing_zeros, 1024);
			if (rc != 0) {
				fprintf(stderr, "err: trailing zero "
					"verification failed!\n");
				__hexdump(stderr, obuff + size, 1024);
				exit_code = EX_ERR_VERIFY;
			}

		} else
			fprintf(stderr, "warn: Verification works currently "
				"only with HOST_DRAM\n");
	}
	// Display the time of the action call (MMIO registers filled + execution)
	fprintf(stdout, "OCACCEL helloworld took %lld usec\n",
		(long long)timediff_usec(&etime, &stime));

	// Detach action + disallocate the card
	ocaccel_detach_action(action);
	ocaccel_card_free(card);

	__free(obuff);
	__free(ibuff);
	exit(exit_code);

 out_error2:
	ocaccel_detach_action(action);
 out_error1:
	ocaccel_card_free(card);
 out_error:
	__free(obuff);
	__free(ibuff);
	exit(EXIT_FAILURE);
}