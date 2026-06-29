/*
 * Snepulator Single-Step Tests harness.
 *
 * Note: These tests are being used to check that the final state matches the
 *       expected final state. The bus states during the instruction are ignored.
 *
 * To do:
 *  - Cycle counts
 *  - User-mode
 *  - Memory exceptions
 */


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include "util.h"
#include "../libraries/cJSON-1.7.19/cJSON.h"

#include "../source/snepulator.h"
#include "../source/cpu/m68k.h"

extern Snepulator_State state;

/* Used to redirect instruction logging */
int null_fd = 0;
int stdout_backup_fd = 0;

#define TEST_DIR "m68000/v1/"
#define COLOUR_RED      "\033[0;31m"
#define COLOUR_GREEN    "\033[0;32m"
#define COLOUR_YELLOW   "\033[0;33m"
#define COLOUR_NORMAL   "\033[0m"

#define RESULT_PASS 0
#define RESULT_FAIL 1
#define RESULT_SKIP 2

static uint32_t test_total;
static uint32_t pass_total;
static uint32_t skip_total;

typedef struct Test_Context_s {
    M68000_Context *m68k_context;
    uint8_t ram [16 << 20]; /* 16 MiB */
    bool skip_test;
} Test_Context;


/*
 * Always report no interrupts.
 */
static uint8_t no_interrupt (void *context_ptr)
{
    return 0;
}


/*
 * Memory read into RAM array.
 */
static uint8_t memory_read_8 (void *context_ptr, uint32_t addr)
{
    Test_Context *context = (Test_Context *) context_ptr;

    addr &= 0xffffff;
    return context->ram [addr];
}


/*
 * Memory write into RAM array.
 */
static void memory_write_8 (void *context_ptr, uint32_t addr, uint8_t data)
{
    Test_Context *context = (Test_Context *) context_ptr;

    addr &= 0xffffff;
    context->ram [addr] = data;
}


/*
 * Memory read into RAM array.
 */
static uint16_t memory_read_16 (void *context_ptr, uint32_t addr)
{
    Test_Context *context = (Test_Context *) context_ptr;

    /* Skip memory exceptions */
    if (addr & 0x000001)
    {
        context->skip_test = true;
    }

    addr &= 0xffffff;
    return util_ntoh16 (* (uint16_t *) &context->ram [addr]);
}


/*
 * Memory write into RAM array.
 */
static void memory_write_16 (void *context_ptr, uint32_t addr, uint16_t data)
{
    Test_Context *context = (Test_Context *) context_ptr;

    /* Skip memory exceptions */
    if (addr & 0x000001)
    {
        context->skip_test = true;
    }

    addr &= 0xffffff;
    * (uint16_t *) & context->ram [addr] = util_hton16 (data);
}


/*
 * Save the current stack-pointer from state.a [7] into either state.ssp
 * or state.usp. This is done before altering the supervisor bit.
 */
static inline void m68k_store_stack_pointer (M68000_Context *context)
{
    if (context->state.sr_supervisor)
    {
        context->state.ssp = context->state.a [7];
    }
    else
    {
        context->state.usp = context->state.a [7];
    }
}


/*
 * Load the current stack-pointer from either state.ssp or state.usp
 * into state.a [7]. This is done after altering the supervisor bit.
 */
static inline void m68k_load_stack_pointer (M68000_Context *context)
{
    context->state.a [7] = (context->state.sr_supervisor) ? context->state.ssp
                                                          : context->state.usp;
}


/*
 * Read state from JSON - Note: Assumes expected fields exist.
 */
static void read_state_from_json (Test_Context *context, cJSON *state)
{
    /* Subtract 4 to account for not doing prefetch */
    context->m68k_context->state.pc = cJSON_GetObjectItemCaseSensitive (state, "pc")->valuedouble - 4;
    context->m68k_context->state.d [0].l = cJSON_GetObjectItemCaseSensitive (state, "d0")->valuedouble;
    context->m68k_context->state.d [1].l = cJSON_GetObjectItemCaseSensitive (state, "d1")->valuedouble;
    context->m68k_context->state.d [2].l = cJSON_GetObjectItemCaseSensitive (state, "d2")->valuedouble;
    context->m68k_context->state.d [3].l = cJSON_GetObjectItemCaseSensitive (state, "d3")->valuedouble;
    context->m68k_context->state.d [4].l = cJSON_GetObjectItemCaseSensitive (state, "d4")->valuedouble;
    context->m68k_context->state.d [5].l = cJSON_GetObjectItemCaseSensitive (state, "d5")->valuedouble;
    context->m68k_context->state.d [6].l = cJSON_GetObjectItemCaseSensitive (state, "d6")->valuedouble;
    context->m68k_context->state.d [7].l = cJSON_GetObjectItemCaseSensitive (state, "d7")->valuedouble;
    context->m68k_context->state.a [0] = cJSON_GetObjectItemCaseSensitive (state, "a0")->valuedouble;
    context->m68k_context->state.a [1] = cJSON_GetObjectItemCaseSensitive (state, "a1")->valuedouble;
    context->m68k_context->state.a [2] = cJSON_GetObjectItemCaseSensitive (state, "a2")->valuedouble;
    context->m68k_context->state.a [3] = cJSON_GetObjectItemCaseSensitive (state, "a3")->valuedouble;
    context->m68k_context->state.a [4] = cJSON_GetObjectItemCaseSensitive (state, "a4")->valuedouble;
    context->m68k_context->state.a [5] = cJSON_GetObjectItemCaseSensitive (state, "a5")->valuedouble;
    context->m68k_context->state.a [6] = cJSON_GetObjectItemCaseSensitive (state, "a6")->valuedouble;
    context->m68k_context->state.ssp = cJSON_GetObjectItemCaseSensitive (state, "ssp")->valuedouble;
    context->m68k_context->state.usp = cJSON_GetObjectItemCaseSensitive (state, "usp")->valuedouble;
    context->m68k_context->state.sr = cJSON_GetObjectItemCaseSensitive (state, "sr")->valuedouble;

    m68k_load_stack_pointer (context->m68k_context);

    cJSON *ram = cJSON_GetObjectItemCaseSensitive (state, "ram");
    cJSON *row = NULL;
    cJSON_ArrayForEach (row, ram)
    {
        cJSON *address_json = cJSON_GetArrayItem (row, 0);
        uint32_t address = address_json->valuedouble;
        cJSON *data = cJSON_GetArrayItem (row, 1);
        context->ram [address & 0xffffff] = data->valueint;
    }
}


extern uint32_t (*m68k_instruction [SIZE_64K]) (M68000_Context *, uint16_t);


/*
 * Run a single test, returns true if the test passes.
 */
static uint32_t run_test (const cJSON *test, bool print_details)
{
    uint32_t result = RESULT_FAIL;
    Test_Context *test_context = calloc (1, sizeof (Test_Context));
    Test_Context *final_context = calloc (1, sizeof (Test_Context));

    M68000_Context *m68k_context = m68k_init (test_context,
                                             memory_read_16, memory_write_16,
                                             memory_read_8, memory_write_8,
                                             no_interrupt);
    test_context->m68k_context = m68k_context;

    M68000_Context *final_m68k_context = m68k_init (final_context, NULL, NULL, NULL, NULL, NULL);
    final_context->m68k_context = final_m68k_context;

    /* Read Initial State */
    cJSON *initial = cJSON_GetObjectItemCaseSensitive (test, "initial");
    if (initial == NULL)
    {
        fprintf (stderr, "Test missing initial state.\n");
        return false;
    }
    read_state_from_json (test_context, initial);

    /* Read Final State */
    cJSON *final = cJSON_GetObjectItemCaseSensitive (test, "final");
    if (final == NULL)
    {
        fprintf (stderr, "Test missing final state.\n");
        return false;
    }
    read_state_from_json (final_context, final);

    /* Skip if the instruction has not been implemented */
    uint16_t opcode = memory_read_16 (test_context, m68k_context->state.pc);
    if (m68k_instruction [opcode] == NULL)
    {
        result = RESULT_SKIP;
    }
    else
    {
        /* TEMP: Disable stdout while running the m68k implementation as it
         *       currently logs every instruction unconditionally */
        fflush (stdout);
        dup2 (null_fd, STDOUT_FILENO);

        /* TODO: Accurate cycle counting has not yet been implemented, instead
         *       Snepulator assumes a 10 cycles per instruction. Assume that each
         *       test runs exactly one instruction. */
        m68k_run_cycles (m68k_context, 10);

        fflush (stdout);
        dup2 (stdout_backup_fd, STDOUT_FILENO);

        /* Skip memory exceptions */
        if (m68k_context->state.pc & 0x000001)
        {
            test_context->skip_test = true;
        }

        if (test_context->skip_test)
        {
            result = RESULT_SKIP;
        }

    }

    if (result != RESULT_SKIP)
    {
        m68k_store_stack_pointer (m68k_context);

        /* Compare final state */
        /* Note: For now the flags are masked with 0xd7 to ignore the undocumented X and Y flags */
        if (m68k_context->state.pc == final_m68k_context->state.pc &&
            m68k_context->state.d [0].l == final_m68k_context->state.d [0].l &&
            m68k_context->state.d [1].l == final_m68k_context->state.d [1].l &&
            m68k_context->state.d [2].l == final_m68k_context->state.d [2].l &&
            m68k_context->state.d [3].l == final_m68k_context->state.d [3].l &&
            m68k_context->state.d [4].l == final_m68k_context->state.d [4].l &&
            m68k_context->state.d [5].l == final_m68k_context->state.d [5].l &&
            m68k_context->state.d [6].l == final_m68k_context->state.d [6].l &&
            m68k_context->state.d [7].l == final_m68k_context->state.d [7].l &&
            m68k_context->state.a [0] == final_m68k_context->state.a [0] &&
            m68k_context->state.a [1] == final_m68k_context->state.a [1] &&
            m68k_context->state.a [2] == final_m68k_context->state.a [2] &&
            m68k_context->state.a [3] == final_m68k_context->state.a [3] &&
            m68k_context->state.a [4] == final_m68k_context->state.a [4] &&
            m68k_context->state.a [5] == final_m68k_context->state.a [5] &&
            m68k_context->state.a [6] == final_m68k_context->state.a [6] &&
            m68k_context->state.ssp == final_m68k_context->state.ssp &&
            m68k_context->state.usp == final_m68k_context->state.usp &&
            m68k_context->state.sr == final_m68k_context->state.sr &&
            memcmp (test_context->ram, final_context->ram, sizeof (test_context->ram)) == 0)
        {
            result = RESULT_PASS;
        }
        else if (print_details)
        {
            printf ("Failed opcode: %04x.\n", opcode);
            if (m68k_context->state.pc != final_m68k_context->state.pc)
            {
                printf ("     Calculated pc=%08x. Expected pc=%08x.\n", m68k_context->state.pc, final_m68k_context->state.pc);
            }
            for (uint32_t dn = 0; dn < 8; dn++)
            {
                if (m68k_context->state.d [dn].l != final_m68k_context->state.d [dn].l)
                {
                    printf ("     Calculated d%d=%08x. Expected d%d=%08x.\n", dn, m68k_context->state.d [dn].l,
                                                                              dn, final_m68k_context->state.d [dn].l);
                }
            }
            for (uint32_t an = 0; an < 7; an++)
            {
                if (m68k_context->state.a [an] != final_m68k_context->state.a [an])
                {
                    printf ("     Calculated a%d=%08x. Expected a%d=%08x.\n", an, m68k_context->state.a [an],
                                                                              an, final_m68k_context->state.a [an]);
                }
            }
            if (m68k_context->state.ssp != final_m68k_context->state.ssp)
            {
                printf ("     Calculated ssp=%08x. Expected ssp=%08x.\n", m68k_context->state.ssp, final_m68k_context->state.ssp);
            }
            if (m68k_context->state.usp != final_m68k_context->state.usp)
            {
                printf ("     Calculated usp=%08x. Expected usp=%08x.\n", m68k_context->state.usp, final_m68k_context->state.usp);
            }
            if (m68k_context->state.sr != final_m68k_context->state.sr)
            {
                printf ("     Calculated sr=%08x. Expected sr=%08x.\n", m68k_context->state.sr, final_m68k_context->state.sr);
                if (m68k_context->state.ccr_carry != final_m68k_context->state.ccr_carry)
                {
                    printf ("       -> Cary flag doesn't match.\n");
                }
                if (m68k_context->state.ccr_overflow != final_m68k_context->state.ccr_overflow)
                {
                    printf ("       -> Overflow flag doesn't match.\n");
                }
                if (m68k_context->state.ccr_zero != final_m68k_context->state.ccr_zero)
                {
                    printf ("       -> Zero flag doesn't match.\n");
                }
                if (m68k_context->state.ccr_negative != final_m68k_context->state.ccr_negative)
                {
                    printf ("       -> Negative flag doesn't match.\n");
                }
                if (m68k_context->state.ccr_extend != final_m68k_context->state.ccr_extend)
                {
                    printf ("       -> Extend flag doesn't match.\n");
                }
            }
            if (memcmp (test_context->ram, final_context->ram, sizeof (test_context->ram)) != 0)
            {
                printf ("     RAM content does not match expected value.\n");
                for (uint32_t addr = 0x000000; addr <= 0xffffff; addr += 2)
                {
                    uint16_t calculated = memory_read_16 (test_context, addr);
                    uint16_t expected = memory_read_16 (final_context, addr);
                    if (calculated != expected)
                    {
                        printf ("     [%06x] calculated %04x, expected %04x.\n", addr, calculated, expected);
                    }
                }
            }
        }
    }

    free (m68k_context);
    free (final_m68k_context);
    free (test_context);
    free (final_context);

    return result;
}


/*
 * Process a single test file
 */
static void run_test_file (char *filename)
{
    int32_t ret;
    uint32_t test_count = 0;
    uint32_t pass_count = 0;
    uint32_t fail_count = 0;
    uint32_t skip_count = 0;

    printf ("File %18s:", filename);

    /* Construct file path */
    char path [PATH_MAX] = { '\0' };
    snprintf (path, PATH_MAX, "%s%s", TEST_DIR, filename);

    /* Load the file into a buffer */
    char *buffer = NULL;
    ret = util_load_file (&buffer, path);
    if (ret < 0)
    {
        fprintf (stderr, "util_load_file failed.\n");
        return;
    }

    cJSON *file_json = cJSON_Parse (buffer);
    if (!file_json)
    {
        const char *error = cJSON_GetErrorPtr ();
        fprintf (stderr, "cJSON Error: %s.\n", error);
        free (buffer);
        return;
    }

    const cJSON *test = NULL;
    cJSON_ArrayForEach (test, file_json)
    {
        cJSON *test_name = cJSON_GetObjectItemCaseSensitive (test, "name");

        if (!cJSON_IsString (test_name) || test_name->valuestring == NULL)
        {
            fprintf (stderr, "Error: Unable to find test name.\n");
            continue;
        }

        uint32_t result = run_test (test, false);

        if (result == RESULT_PASS)
        {
            pass_count += 1;
        }

        if (result == RESULT_FAIL)
        {
            fail_count += 1;

            /* Only take up multiple lines if at least one test in this file fails. */
            if (fail_count == 1)
            {
                printf ("\n");
            }

            /* Only print up to three specific failure cases for each file */
            if (fail_count <= 3)
            {
                printf ("  -> Test '%s' failed:\n", test_name->valuestring);
                run_test (test, true);
            }
        }

        if (result == RESULT_SKIP)
        {
            skip_count += 1;
        }
        else
        {
            test_count += 1;
        }
    }

    if (fail_count != 0)
    {
        printf (COLOUR_RED);

        /* Re-align result text after detailed failure output */
        printf ("                        ");
    }
    else if (skip_count != 0)
    {
        printf (COLOUR_YELLOW);
    }
    else
    {
        printf (COLOUR_GREEN);
    }

    if (skip_count)
    {
        printf ("  Passed %4d / %4d tests. (%d skipped)\n", pass_count, test_count, skip_count);
    }
    else
    {
        printf ("  Passed %4d / %4d tests.\n", pass_count, test_count);
    }
    printf (COLOUR_NORMAL);
    pass_total += pass_count;
    test_total += test_count;
    skip_total += skip_count;

    cJSON_Delete (file_json);
    free (buffer);
}


/*
 * Loop over all test files for the z80.
 */
int main (int argc, char **argv)
{
    char *filter = NULL;

    /* Optionally filter to run only a subset of tests */
    if (argc == 3 && !strcmp (argv [1], "--filter"))
    {
        filter = argv [2];
    }
    else if (argc != 1)
    {
        fprintf (stderr, "Usage: %s [--filter <filter>]\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Create and destroy an initial context, to get through
     * logs generated as part of instruction registration. */
    M68000_Context *m68k_first_context = m68k_init (NULL, NULL, NULL, NULL, NULL, NULL);
    free (m68k_first_context);

    /* Mark the state as running so the m68k implementation doesn't exit early. */
    state.run = RUN_STATE_RUNNING;

    /* Open /dev/null, used to redirect instruction logging */
    null_fd = open ("/dev/null", O_WRONLY);
    if (null_fd < 0) {
        printf ("Unable to open /dev/null.\n");
        return EXIT_FAILURE;
    }

    stdout_backup_fd = dup (STDOUT_FILENO);

    DIR *dir = opendir (TEST_DIR);
    if (dir == NULL)
    {
        fprintf (stderr, "Unable to open directory '%s'.\n", TEST_DIR);
        return EXIT_FAILURE;
    }

    /* Loop over all test files */
    struct dirent *entry = NULL;

    for (entry = readdir (dir); entry != NULL; entry = readdir (dir))
    {
        /* Ignore "." and ".." directories, or anything hidden. */
        if (entry->d_name[0] == '.')
        {
            continue;
        }

        /* Make sure this is a json file */
        char *extension = strstr (entry->d_name, ".json");
        if (extension == NULL || strlen (extension) != 5)
        {
            continue;
        }

        /* Optional filtering to quickly test specific instructions */
        if (filter && !strstr (entry->d_name, filter))
        {
            continue;
        }

        run_test_file (entry->d_name);
    }

    closedir (dir);

    printf ("%s", pass_total == test_total ? COLOUR_GREEN : COLOUR_RED);
    printf ("Passed total: %d / %d. %.2f%% (%d skipped)\n", pass_total, test_total, (double) pass_total * 100.0 / test_total, skip_total);
    printf (COLOUR_NORMAL);

    return EXIT_SUCCESS;
}
