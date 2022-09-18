/** \file
 * \brief Example code for Simple Open EtherCAT master using the context based api.
 *
 * Usage : simple_test_ctx_api [ifname1]
 * ifname is NIC interface, f.e. eth0
 *
 * This is a minimal test.
 *
 * (c)Arthur Ketels 2010 - 2011
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <ethercat.h>

#define EC_TIMEOUTMON 500

char IOmap[4096];
OSAL_THREAD_HANDLE thread1;
int expectedWKC;
boolean needlf;
volatile int wkc;
boolean inOP;
uint8 currentgroup = 0;

struct ecx_context_memory_holder ctx_mem;
ecx_contextt ctx;

void simpletest(char *ifname) {
    int i, j,oloop, iloop, chk;
    needlf = FALSE;
    inOP = FALSE;

    /* setup context */
    ecx_initialize_context(&ctx, &ctx_mem);

    printf("Starting simple test\n");

    /* initialise SOEM, bind socket to ifname */
    if (ecx_init(&ctx, ifname)) {
        printf("ecx_init on %s succeeded.\n", ifname);
        /* find and auto-config slaves */


        if (ecx_config_init(&ctx, FALSE) > 0) {
            printf("%d slaves found and configured.\n", *(ctx.slavecount));

            ecx_config_map_group(&ctx, &IOmap, 0);

            ecx_configdc(&ctx);

            printf("Slaves mapped, state to SAFE_OP.\n");
            /* wait for all slaves to reach SAFE_OP state */
            ecx_statecheck(&ctx, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);

            oloop = (int) ctx.slavelist[0].Obytes;
            if ((oloop == 0) && (ctx.slavelist[0].Obits > 0)) oloop = 1;
            if (oloop > 8) oloop = 8;
            iloop = (int) ctx.slavelist[0].Ibytes;
            if ((iloop == 0) && (ctx.slavelist[0].Ibits > 0)) iloop = 1;
            if (iloop > 8) iloop = 8;

            printf("segments : %d : %d %d %d %d\n", ctx.grouplist[0].nsegments, ctx.grouplist[0].IOsegment[0],
                   ctx.grouplist[0].IOsegment[1], ctx.grouplist[0].IOsegment[2], ctx.grouplist[0].IOsegment[3]);

            printf("Request operational state for all slaves\n");
            expectedWKC = (ctx.grouplist[0].outputsWKC * 2) + ctx.grouplist[0].inputsWKC;
            printf("Calculated workcounter %d\n", expectedWKC);
            ctx.slavelist[0].state = EC_STATE_OPERATIONAL;
            /* send one valid process data to make outputs in slaves happy*/
            ecx_send_processdata(&ctx);
            ecx_receive_processdata(&ctx, EC_TIMEOUTRET);
            /* request OP state for all slaves */
            ecx_writestate(&ctx,0);
            chk = 200;
            /* wait for all slaves to reach OP state */
            do {
                ecx_send_processdata(&ctx);
                ecx_receive_processdata(&ctx, EC_TIMEOUTRET);
                ecx_statecheck(&ctx,0, EC_STATE_OPERATIONAL, 50000);
            } while (chk-- && (ctx.slavelist[0].state != EC_STATE_OPERATIONAL));
            if (ctx.slavelist[0].state == EC_STATE_OPERATIONAL) {
                printf("Operational state reached for all slaves.\n");
                inOP = TRUE;
                /* cyclic loop */
                for (i = 1; i <= 10000; i++) {
                    ecx_send_processdata(&ctx);
                    wkc = ecx_receive_processdata(&ctx,EC_TIMEOUTRET);

                    if (wkc >= expectedWKC) {
                        printf("Processdata cycle %4d, WKC %d , O:", i, wkc);

                        for (j = 0; j < oloop; j++) {
                            printf(" %2.2x", *(ctx.slavelist[0].outputs + j));
                        }

                        printf(" I:");
                        for (j = 0; j < iloop; j++) {
                            printf(" %2.2x", *(ctx.slavelist[0].inputs + j));
                        }
                        printf(" T:%"PRId64"\r", *(ctx.DCtime));
                        needlf = TRUE;
                    }
                    osal_usleep(5000);

                }
                inOP = FALSE;
            } else {
                printf("Not all slaves reached operational state.\n");
                ecx_readstate(&ctx);
                for (i = 1; i <= *(ctx.slavecount); i++) {
                    if (ctx.slavelist[i].state != EC_STATE_OPERATIONAL) {
                        printf("Slave %d State=0x%2.2x StatusCode=0x%4.4x : %s\n",
                               i, ctx.slavelist[i].state, ctx.slavelist[i].ALstatuscode,
                               ec_ALstatuscode2string(ctx.slavelist[i].ALstatuscode));
                    }
                }
            }
            printf("\nRequest init state for all slaves\n");
            ctx.slavelist[0].state = EC_STATE_INIT;
            /* request INIT state for all slaves */
            ecx_writestate(&ctx, 0);
        } else {
            printf("No slaves found!\n");
        }
        printf("End simple test, close socket\n");
        /* stop SOEM, close socket */
        ecx_close(&ctx);
    } else {
        printf("No socket connection on %s\nExecute as root\n", ifname);
    }
}

OSAL_THREAD_FUNC ecatcheck(void *ptr) {
    int slave;
    (void) ptr;                  /* Not used */

    while (1) {
        if (inOP && ((wkc < expectedWKC) || ctx.grouplist[currentgroup].docheckstate)) {
            if (needlf) {
                needlf = FALSE;
                printf("\n");
            }
            /* one ore more slaves are not responding */
            ctx.grouplist[currentgroup].docheckstate = FALSE;
            ecx_readstate(&ctx);
            for (slave = 1; slave <= *(ctx.slavecount); slave++) {
                if ((ctx.slavelist[slave].group == currentgroup) && (ctx.slavelist[slave].state != EC_STATE_OPERATIONAL)) {
                    ctx.grouplist[currentgroup].docheckstate = TRUE;
                    if (ctx.slavelist[slave].state == (EC_STATE_SAFE_OP + EC_STATE_ERROR)) {
                        printf("ERROR : slave %d is in SAFE_OP + ERROR, attempting ack.\n", slave);
                        ctx.slavelist[slave].state = (EC_STATE_SAFE_OP + EC_STATE_ACK);
                        ecx_writestate(&ctx, slave);
                    } else if (ctx.slavelist[slave].state == EC_STATE_SAFE_OP) {
                        printf("WARNING : slave %d is in SAFE_OP, change to OPERATIONAL.\n", slave);
                        ctx.slavelist[slave].state = EC_STATE_OPERATIONAL;
                        ecx_writestate(&ctx, slave);
                    } else if (ctx.slavelist[slave].state > EC_STATE_NONE) {
                        if (ecx_reconfig_slave(&ctx, slave, EC_TIMEOUTMON)) {
                            ctx.slavelist[slave].islost = FALSE;
                            printf("MESSAGE : slave %d reconfigured\n", slave);
                        }
                    } else if (!ctx.slavelist[slave].islost) {
                        /* re-check state */
                        ecx_statecheck(&ctx, slave, EC_STATE_OPERATIONAL, EC_TIMEOUTRET);
                        if (ctx.slavelist[slave].state == EC_STATE_NONE) {
                            ctx.slavelist[slave].islost = TRUE;
                            printf("ERROR : slave %d lost\n", slave);
                        }
                    }
                }
                if (ctx.slavelist[slave].islost) {
                    if (ctx.slavelist[slave].state == EC_STATE_NONE) {
                        if (ecx_recover_slave(&ctx, slave, EC_TIMEOUTMON)) {
                            ctx.slavelist[slave].islost = FALSE;
                            printf("MESSAGE : slave %d recovered\n", slave);
                        }
                    } else {
                        ctx.slavelist[slave].islost = FALSE;
                        printf("MESSAGE : slave %d found\n", slave);
                    }
                }
            }
            if (!ctx.grouplist[currentgroup].docheckstate)
                printf("OK : all slaves resumed OPERATIONAL.\n");
        }
        osal_usleep(10000);
    }
}

int main(int argc, char *argv[]) {
    printf("SOEM (Simple Open EtherCAT Master)\nSimple test\n");

    if (argc > 1) {
        /* create thread to handle slave error handling in OP */
//      pthread_create( &thread1, NULL, (void *) &ecatcheck, (void*) &ctime);
        osal_thread_create(&thread1, 128000, &ecatcheck, (void *) &ctime);
        /* start cyclic part */
        simpletest(argv[1]);
    } else {
        ec_adaptert *adapter = NULL;
        printf("Usage: simple_test_ctx_api ifname1\nifname = eth0 for example\n");

        printf("\nAvailable adapters:\n");
        adapter = ec_find_adapters();
        while (adapter != NULL) {
            printf("    - %s  (%s)\n", adapter->name, adapter->desc);
            adapter = adapter->next;
        }
        ec_free_adapters(adapter);
    }

    printf("End program\n");
    return (0);
}
