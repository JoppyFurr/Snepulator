/*
 * Snepulator Single-Step Tests harness.
 *
 * Note: These tests are being used to check that the final state matches the
 *       expected final state. The bus states during the instruction are ignored.
 *
 * To do list:
 *  - I/O Support
 *  - ei
 *  - Hidden 'wz' register
 *  - X & Y flags
 */


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "util.h"
#include "../libraries/cJSON-1.7.19/cJSON.h"

#include "../source/snepulator.h"
#include "../source/cpu/z80.h"

#define TEST_DIR "z80/v1/"
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
    Z80_Context *z80_context;
    uint8_t ram [SIZE_64K];
    bool skip_test;
} Test_Context;


/*
 * Always report no interrupts.
 */
static bool no_interrupt (void *context_ptr)
{
    return false;
}


/*
 * No I/O support for now.
 */
static uint8_t io_read (void *context_ptr, uint8_t addr)
{
    Test_Context *context = (Test_Context *) context_ptr;

    context->skip_test = true;
    return 0;
}


/*
 * No I/O support for now.
 */
static void io_write (void *context_ptr, uint8_t addr, uint8_t data)
{
    Test_Context *context = (Test_Context *) context_ptr;

    context->skip_test = true;
    return;
}


/*
 * Memory read into RAM array.
 */
static uint8_t memory_read (void *context_ptr, uint16_t addr)
{
    Test_Context *context = (Test_Context *) context_ptr;
    return context->ram [addr];
}


/*
 * Memory write into RAM array.
 */
static void memory_write (void *context_ptr, uint16_t addr, uint8_t data)
{
    Test_Context *context = (Test_Context *) context_ptr;
    context->ram [addr] = data;
}


/*
 * Read state from JSON - Note: Assumes expected fields exist.
 */
static void read_state_from_json (Test_Context *context, cJSON *state)
{
    context->z80_context->state.pc = cJSON_GetObjectItemCaseSensitive (state, "pc")->valueint;
    context->z80_context->state.sp = cJSON_GetObjectItemCaseSensitive (state, "sp")->valueint;
    context->z80_context->state.a = cJSON_GetObjectItemCaseSensitive (state, "a")->valueint;
    context->z80_context->state.b = cJSON_GetObjectItemCaseSensitive (state, "b")->valueint;
    context->z80_context->state.c = cJSON_GetObjectItemCaseSensitive (state, "c")->valueint;
    context->z80_context->state.d = cJSON_GetObjectItemCaseSensitive (state, "d")->valueint;
    context->z80_context->state.e = cJSON_GetObjectItemCaseSensitive (state, "e")->valueint;
    context->z80_context->state.f = cJSON_GetObjectItemCaseSensitive (state, "f")->valueint;
    context->z80_context->state.h = cJSON_GetObjectItemCaseSensitive (state, "h")->valueint;
    context->z80_context->state.l = cJSON_GetObjectItemCaseSensitive (state, "l")->valueint;
    context->z80_context->state.i = cJSON_GetObjectItemCaseSensitive (state, "i")->valueint;
    context->z80_context->state.r = cJSON_GetObjectItemCaseSensitive (state, "r")->valueint;
    /* EI ignored */
    /* WZ ignored */
    context->z80_context->state.ix = cJSON_GetObjectItemCaseSensitive (state, "ix")->valueint;
    context->z80_context->state.iy = cJSON_GetObjectItemCaseSensitive (state, "iy")->valueint;
    context->z80_context->state.af_alt = cJSON_GetObjectItemCaseSensitive (state, "af_")->valueint;
    context->z80_context->state.bc_alt = cJSON_GetObjectItemCaseSensitive (state, "bc_")->valueint;
    context->z80_context->state.de_alt = cJSON_GetObjectItemCaseSensitive (state, "de_")->valueint;
    context->z80_context->state.hl_alt = cJSON_GetObjectItemCaseSensitive (state, "hl_")->valueint;
    context->z80_context->state.im = cJSON_GetObjectItemCaseSensitive (state, "im")->valueint;
    /* P ignored */
    /* Q ignored */
    context->z80_context->state.iff1 = cJSON_GetObjectItemCaseSensitive (state, "iff1")->valueint;
    context->z80_context->state.iff2 = cJSON_GetObjectItemCaseSensitive (state, "iff2")->valueint;

    cJSON *ram = cJSON_GetObjectItemCaseSensitive (state, "ram");
    cJSON *row = NULL;
    cJSON_ArrayForEach (row, ram)
    {
        cJSON *address = cJSON_GetArrayItem (row, 0);
        cJSON *data = cJSON_GetArrayItem (row, 1);
        context->ram [address->valueint & 0xffff] = data->valueint;
    }
}


/*
 * Run a single test, returns true if the test passes.
 */
static uint32_t run_test (const cJSON *test, bool print_details)
{
    uint32_t result = RESULT_FAIL;
    Test_Context test_context = { };
    Test_Context final_context = { };

    Z80_Context *z80_context = z80_init (&test_context, memory_read, memory_write,
                                         io_read, io_write, no_interrupt, no_interrupt);
    test_context.z80_context = z80_context;

    Z80_Context *final_z80_context = z80_init (&final_context, NULL, NULL, NULL, NULL, NULL, NULL);
    final_context.z80_context = final_z80_context;

    /* Read Initial State */
    cJSON *initial = cJSON_GetObjectItemCaseSensitive (test, "initial");
    if (initial == NULL)
    {
        fprintf (stderr, "Test missing initial state.\n");
        return false;
    }
    read_state_from_json (&test_context, initial);

    /* Read Final State */
    cJSON *final = cJSON_GetObjectItemCaseSensitive (test, "final");
    if (final == NULL)
    {
        fprintf (stderr, "Test missing final state.\n");
        return false;
    }
    read_state_from_json (&final_context, final);

    /* Run cycles,
     * For now ignore the cycle-by-cycle bus values, just count how many cycles
     * there are, run them, and then check the result. */
    cJSON *cycles = cJSON_GetObjectItemCaseSensitive (test, "cycles");
    uint32_t cycle_count = cJSON_GetArraySize (cycles);
    z80_run_cycles (z80_context, cycle_count);

    if (test_context.skip_test == true)
    {
        result = RESULT_SKIP;
    }
    else
    {
        /* Compare final state */
        /* Note: For now the flags are masked with 0xd7 to ignore the undocumented X and Y flags */
        if (z80_context->state.a == final_z80_context->state.a &&
#if 0 /* Enable X & Y */
            (z80_context->state.f)  == (final_z80_context->state.f) &&
#else /* Mask X & Y */
            (z80_context->state.f & 0xd7)  == (final_z80_context->state.f & 0xd7) &&
#endif
            z80_context->state.bc == final_z80_context->state.bc &&
            z80_context->state.de == final_z80_context->state.de &&
            z80_context->state.hl == final_z80_context->state.hl &&
            z80_context->state.af_alt == final_z80_context->state.af_alt &&
            z80_context->state.bc_alt == final_z80_context->state.bc_alt &&
            z80_context->state.de_alt == final_z80_context->state.de_alt &&
            z80_context->state.hl_alt == final_z80_context->state.hl_alt &&
            z80_context->state.ir == final_z80_context->state.ir &&
            z80_context->state.ix == final_z80_context->state.ix &&
            z80_context->state.iy == final_z80_context->state.iy &&
            z80_context->state.sp == final_z80_context->state.sp &&
            z80_context->state.pc == final_z80_context->state.pc &&
            z80_context->state.im == final_z80_context->state.im &&
            z80_context->state.iff1 == final_z80_context->state.iff1 &&
            z80_context->state.iff2 == final_z80_context->state.iff2 &&
            memcmp (test_context.ram, final_context.ram, SIZE_64K) == 0)
        {
            result = RESULT_PASS;
        }
        else if (print_details)
        {
            if (z80_context->state.a != final_z80_context->state.a)
            {
                printf ("     Calculated a=%02x. Expected a=%02x.\n", z80_context->state.a, final_z80_context->state.a);
            }
            if (z80_context->state.f != final_z80_context->state.f)
            {
                printf ("     Calculated f=%02x. Expected f=%02x.\n", z80_context->state.f, final_z80_context->state.f);
            }
            if (z80_context->state.b != final_z80_context->state.b)
            {
                printf ("     Calculated b=%02x. Expected b=%02x.\n", z80_context->state.b, final_z80_context->state.b);
            }
            if (z80_context->state.c != final_z80_context->state.c)
            {
                printf ("     Calculated c=%02x. Expected c=%02x.\n", z80_context->state.c, final_z80_context->state.c);
            }
            if (z80_context->state.d != final_z80_context->state.d)
            {
                printf ("     Calculated d=%02x. Expected d=%02x.\n", z80_context->state.d, final_z80_context->state.d);
            }
            if (z80_context->state.e != final_z80_context->state.e)
            {
                printf ("     Calculated e=%02x. Expected e=%02x.\n", z80_context->state.e, final_z80_context->state.e);
            }
            if (z80_context->state.hl != final_z80_context->state.hl)
            {
                printf ("     Calculated hl=%04x. Expected hl=%04x.\n", z80_context->state.hl, final_z80_context->state.hl);
            }
            if (z80_context->state.a_alt != final_z80_context->state.a_alt)
            {
                printf ("     Calculated a_alt=%02x. Expected a_alt=%02x.\n", z80_context->state.a_alt, final_z80_context->state.a_alt);
            }
            if (z80_context->state.f_alt != final_z80_context->state.f_alt)
            {
                printf ("     Calculated f_alt=%02x. Expected f_alt=%02x.\n", z80_context->state.f_alt, final_z80_context->state.f_alt);
            }
            if (z80_context->state.b_alt != final_z80_context->state.b_alt)
            {
                printf ("     Calculated b_alt=%02x. Expected b_alt=%02x.\n", z80_context->state.b_alt, final_z80_context->state.b_alt);
            }
            if (z80_context->state.c_alt != final_z80_context->state.c_alt)
            {
                printf ("     Calculated c_alt=%02x. Expected c_alt=%02x.\n", z80_context->state.c_alt, final_z80_context->state.c_alt);
            }
            if (z80_context->state.d_alt != final_z80_context->state.d_alt)
            {
                printf ("     Calculated d_alt=%02x. Expected d_alt=%02x.\n", z80_context->state.d_alt, final_z80_context->state.d_alt);
            }
            if (z80_context->state.e_alt != final_z80_context->state.e_alt)
            {
                printf ("     Calculated e_alt=%02x. Expected e_alt=%02x.\n", z80_context->state.e_alt, final_z80_context->state.e_alt);
            }
            if (z80_context->state.hl_alt != final_z80_context->state.hl_alt)
            {
                printf ("     Calculated hl_alt=%04x. Expected hl_alt=%04x.\n", z80_context->state.hl_alt, final_z80_context->state.hl_alt);
            }
            if (z80_context->state.i != final_z80_context->state.i)
            {
                printf ("     Calculated i=%02x. Expected i=%02x.\n", z80_context->state.i, final_z80_context->state.i);
            }
            if (z80_context->state.r != final_z80_context->state.r)
            {
                printf ("     Calculated r=%02x. Expected r=%02x.\n", z80_context->state.r, final_z80_context->state.r);
            }
            if (z80_context->state.ix != final_z80_context->state.ix)
            {
                printf ("     Calculated ix=%04x. Expected ix=%04x.\n", z80_context->state.ix, final_z80_context->state.ix);
            }
            if (z80_context->state.iy != final_z80_context->state.iy)
            {
                printf ("     Calculated iy=%04x. Expected iy=%04x.\n", z80_context->state.iy, final_z80_context->state.iy);
            }
            if (z80_context->state.sp != final_z80_context->state.sp)
            {
                printf ("     Calculated sp=%04x. Expected sp=%04x.\n", z80_context->state.sp, final_z80_context->state.sp);
            }
            if (z80_context->state.pc != final_z80_context->state.pc)
            {
                printf ("     Calculated pc=%04x. Expected pc=%04x.\n", z80_context->state.pc, final_z80_context->state.pc);
            }
            if (z80_context->state.im != final_z80_context->state.im)
            {
                printf ("     Calculated im=%d. Expected im=%d.\n", z80_context->state.im, final_z80_context->state.im);
            }
            if (z80_context->state.iff1 != final_z80_context->state.iff1)
            {
                printf ("     Calculated iff1=%d. Expected iff1=%d.\n", z80_context->state.iff1, final_z80_context->state.iff1);
            }
            if (z80_context->state.iff2 != final_z80_context->state.iff2)
            {
                printf ("     Calculated iff2=%d. Expected iff2=%d.\n", z80_context->state.iff2, final_z80_context->state.iff2);
            }
            if (memcmp (test_context.ram, final_context.ram, SIZE_64K) != 0)
            {
                printf ("     RAM content does not match expected value.\n");
            }
        }
    }


    free (z80_context);
    free (final_z80_context);

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
