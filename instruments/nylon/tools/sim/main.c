// tools/sim: ./run.sh            everything: the core's checks, then the instrument's (test_inst.c)
//            ./run.sh inst       only the instrument's scenarios (faster while working on the sound)
#include "sim.h"
#include <string.h>
void core_checks(void);
void core_health(void);
void sim_finish(void);
int main(int argc, char **argv)
{
    bool only_inst = argc > 1 && !strcmp(argv[1], "inst");
    sim_boot(true);
    if (!only_inst) core_checks();
    section("instrument");
    inst_tests();
    core_health();
    sim_finish();
    printf("\n%d checks, %d failed\n", checks_run, checks_failed);
    return checks_failed ? 1 : 0;
}
