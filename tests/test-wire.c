/* vim: set ts=4 sw=4: */

#include "qemu-common.h"
#include "hw/wire.h"
#include "hw/irq.h"
#include "qom/object.h"

#include <glib.h>

static const char strength_code[8] = "zsmw\0\0\0\v";

/*
 * A helper listener that records wire events in a string.
 * This allows us to use strcmp to tests if the right events
 * were fired.
 * For example, a clock looks like "010101" and a line switching
 * from a strong 1 to a weak 0 then to Hi-Z looks like "10wz".
 */
static void recorder_handler(void *opaque, qemu_wire wire)
{
    char *p = opaque;
    unsigned strength;
    bool value;
    char code;

    p += strlen(p);

    value = qemu_wire_sense(wire, &strength);
    code = strength_code[strength];

    if (qemu_wire_sense_conflicted(wire))
        *p++ = 'C';
    if (strength != STRENGTH_HI_Z)
        *p++ = value ? '1' : '0';
    if (strength > STRENGTH_MAX)
        *p++ = '?';
    else if (code)
        *p++ = code;
    *p = '\0';
}

static void recorder_multi_handler(void *opaque, uint32_t value,
                                   unsigned weakest_strength, unsigned n,
                                   const qemu_wire *wires)
{
    char *p = opaque;
    char code;
    unsigned i;

    p += strlen(p);

    *p++ = '<';
    for (i = n; i--;) {
        unsigned strength;
        qemu_wire wire = wires[i];
        bool value;

        if (qemu_wire_sense_conflicted(wire)) {
            *p++ = 'C';
            continue;
        }
        value = qemu_wire_sense(wire, &strength);
        if (strength == STRENGTH_HI_Z)
            *p++ = 'z';
        else
            *p++ = value ? '1' : '0';
    }
    *p++ = '>';
    code = strength_code[weakest_strength];
    if (code)
        *p++ = code;
    *p = '\0';
}


static void test_wire_digital(void)
{
    bool value;
    unsigned strength;
    qemu_wire wire;
    qemu_wiredriver driver, driver2;

    wire = qemu_allocate_wire();

    strength = ~0;
    value = qemu_wire_sense(wire, &strength);
    g_assert_cmpuint(strength,==,STRENGTH_HI_Z);
    g_assert(WIRE_IS_HI_Z(wire));

    driver = qemu_allocate_wiredriver(wire);

    qemu_wire_drive(driver, STRENGTH_DEFAULT, true);
    strength = ~0;
    value = qemu_wire_sense(wire, &strength);
    g_assert(value);
    g_assert_cmpuint(strength,==,STRENGTH_DEFAULT);
    g_assert(!WIRE_IS_HI_Z(wire));

    qemu_wire_drive(driver, STRENGTH_HI_Z, true);
    strength = ~0;
    value = qemu_wire_sense(wire, &strength);
    g_assert_cmpuint(strength,==,STRENGTH_HI_Z);
    g_assert(WIRE_IS_HI_Z(wire));

    driver2 = qemu_allocate_wiredriver(wire);

    g_assert(WIRE_IS_HI_Z(wire));

    qemu_wire_drive(driver2, STRENGTH_DEFAULT, true);
    strength = ~0;
    value = qemu_wire_sense(wire, &strength);
    g_assert(value);
    g_assert_cmpuint(strength,==,STRENGTH_DEFAULT);
    g_assert(!WIRE_IS_HI_Z(wire));

    qemu_wire_drive(driver, STRENGTH_WEAK, false);
    strength = ~0;
    value = qemu_wire_sense(wire, &strength);
    g_assert(value);
    g_assert_cmpuint(strength,==,STRENGTH_DEFAULT);
    g_assert(!WIRE_IS_HI_Z(wire));

    qemu_wire_drive(driver, STRENGTH_STRONG, false);
    strength = ~0;
    value = qemu_wire_sense(wire, &strength);
    g_assert(!value);
    g_assert_cmpuint(strength,==,STRENGTH_STRONG);
    g_assert(!WIRE_IS_HI_Z(wire));

    qemu_free_wiredriver(driver2);
    qemu_free_wiredriver(driver);
    qemu_free_wire(wire);
}

static void test_wire_analog(void)
{
    int value;
    unsigned strength;
    qemu_wire wire;
    qemu_wiredriver driver;

    wire = qemu_allocate_wire();
    driver = qemu_allocate_wiredriver(wire);

    qemu_wire_drive_a(driver, STRENGTH_DEFAULT, 12345);
    value = qemu_wire_sense_a(wire, &strength);
    g_assert_cmpint(value,==,12345);
    g_assert_cmpuint(strength,==,STRENGTH_DEFAULT);
    g_assert(!WIRE_IS_HI_Z(wire));

    qemu_wire_drive_a(driver, STRENGTH_HI_Z, 67890);
    value = qemu_wire_sense_a(wire, &strength);
    g_assert(WIRE_IS_HI_Z(wire));

    qemu_free_wiredriver(driver);
    qemu_free_wire(wire);
}

static void test_wire_mixed(void)
{
    int avalue;
    bool dvalue;
    unsigned strength;
    qemu_wire wire;
    qemu_wiredriver driver_a;
    qemu_wiredriver driver_d;

    wire = qemu_allocate_wire();
    driver_a = qemu_allocate_wiredriver(wire);
    driver_d = qemu_allocate_wiredriver(wire);

    qemu_wire_drive_a(driver_a, STRENGTH_DEFAULT, 12345);
    g_assert(!qemu_wire_sense_conflicted(wire));

    avalue = qemu_wire_sense_a(wire, &strength);
    g_assert_cmpuint(strength,==,STRENGTH_DEFAULT);
    g_assert_cmpint(avalue,==,12345);


    dvalue = qemu_wire_sense(wire, &strength);
    g_assert_cmpuint(strength,==,STRENGTH_DEFAULT);
    g_assert(12345 < (WIRE_INTRINSIC_DEFAULT/2));
    g_assert(!dvalue);

    qemu_wire_drive(driver_d, STRENGTH_DEFAULT, false);
    g_assert(qemu_wire_sense_conflicted(wire));
    qemu_wire_drive_z(driver_a);
    g_assert(!qemu_wire_sense_conflicted(wire));

    avalue = qemu_wire_sense_a(wire, &strength);
    dvalue = qemu_wire_sense(wire, &strength);
    g_assert(!dvalue);
    g_assert_cmpint(avalue,==,0);

    qemu_wire_drive(driver_d, STRENGTH_DEFAULT, true);
    g_assert(!qemu_wire_sense_conflicted(wire));

    avalue = qemu_wire_sense_a(wire, &strength);
    dvalue = qemu_wire_sense(wire, &strength);
    g_assert_cmpint(avalue,==,WIRE_INTRINSIC_DEFAULT);
    g_assert(dvalue);

    qemu_free_wiredriver(driver_a);
    qemu_free_wiredriver(driver_d);
    qemu_free_wire(wire);
}

static void test_wire_listen(void)
{
    qemu_wire wire;
    qemu_wiredriver driver1, driver2;
    char buf[256] = "";

    wire = qemu_allocate_wire();
    qemu_wire_listen(wire, recorder_handler, buf);

    driver1 = qemu_allocate_wiredriver(wire);
    driver2 = qemu_allocate_wiredriver(wire);
    g_assert_cmpstr(buf,==,"");

    qemu_wire_drive(driver1, STRENGTH_DEFAULT, true);
    qemu_wire_drive(driver1, STRENGTH_DEFAULT, false);
    qemu_wire_drive_z(driver1);
    g_assert_cmpstr(buf,==,"10z");

    qemu_wire_unlisten(wire, recorder_handler, buf);
    qemu_free_wire(wire);
    qemu_free_wiredriver(driver1);
    qemu_free_wiredriver(driver2);
}

static void test_wire_null(void)
{
    qemu_wire wire;
    qemu_wiredriver driver;
    void *token;

    wire = qemu_allocate_wire();
    driver = qemu_allocate_wiredriver(NULL);

    qemu_free_wiredriver(NULL);
    qemu_free_wire(NULL);
    qemu_wire_attach(NULL, NULL);
    qemu_wire_attach(NULL, driver);
    qemu_wire_detach(NULL, NULL);
    qemu_wire_detach(NULL, driver);

    qemu_wire_listen(NULL, recorder_handler, NULL);
    qemu_wire_unlisten(NULL, recorder_handler, NULL);

    token = qemu_wire_multi_listen(NULL, 0, recorder_multi_handler, NULL);
    g_assert(token == NULL);
    qemu_wire_multi_unlisten(NULL);

    qemu_free_wiredriver(driver);
    qemu_free_wire(wire);
}

static void test_wire_multi(void)
{
    qemu_wire wire0, wire1;
    qemu_wiredriver driver1, driver2;
    char buf0[32] = "";
    char buf1[32] = "";
    char bufm[256] = "";
    void *token;
    uint32_t mvalue;
    unsigned strength;

    wire0 = qemu_allocate_wire();
    wire1 = qemu_allocate_wire();
    driver1 = qemu_allocate_wiredriver(NULL);
    driver2 = qemu_allocate_wiredriver(NULL);

    /* Driver 1 drives both wires 0 and 1 */
    qemu_wire_attach(wire0, driver1);
    qemu_wire_attach(wire1, driver1);
    /* Driver 2 drives only wire 1 */
    qemu_wire_attach(wire1, driver2);

    /*
     * wire0 <- driver1
     * wire1 <- (driver1 + driver2)
     */

    qemu_wire_listen(wire0, recorder_handler, buf0);
    qemu_wire_listen(wire1, recorder_handler, buf1);

    token = qemu_wire_multi_listen((qemu_wire[]){wire0, wire1}, 2,
       recorder_multi_handler, bufm);

    /*
     * wire0: z         <- z
     * wire1: z         <- z + z
     * wirem: <zz>
     */

    /* The following should result in <01> */
    qemu_wire_multi_drive((struct qemu_wire_drive[]) {      /* #1 */
        { .driver = driver1,
          .value = 1,                       /* 1w */
          .strength = STRENGTH_WEAK,
          .value_mode = VALUE_MODE_D },
        { .driver = driver2,
          .value = 0,                       /* 0s */
          .strength = STRENGTH_STRONG,
          .value_mode = VALUE_MODE_D },
    }, 2);

    /*
     * wire0: 1w        <- 1w
     * wire1: 0s        <- 1w + 0s
     * wirem: <01>
     */
    mvalue = qemu_wire_multi_sense((qemu_wire[]){wire0, wire1}, 2, &strength);
    g_assert(!WIRE_IS_HI_Z(wire0));
    g_assert(!WIRE_IS_HI_Z(wire1));
    g_assert(qemu_wire_sense(wire0, NULL));
    g_assert(!qemu_wire_sense(wire1, NULL));
    g_assert_cmpuint(mvalue,==,1); /* 0b01 */
    g_assert_cmpuint(strength,==,STRENGTH_WEAK); /* weakest of {w,s} */

    /* Bringing driver2 to hi-z should result in <11> */
    qemu_wire_drive_z(driver2);                             /* #2 */

    /*
     * wire0: 1w        <- 1w
     * wire1: 1w        <- 1w + z
     * wirem: <11>
     */
    mvalue = qemu_wire_multi_sense((qemu_wire[]){wire0, wire1}, 2, &strength);
    g_assert(!WIRE_IS_HI_Z(wire0));
    g_assert(!WIRE_IS_HI_Z(wire1));
    g_assert(qemu_wire_sense(wire0, NULL));
    g_assert(qemu_wire_sense(wire1, NULL));
    g_assert_cmpuint(mvalue,==,3); /* 0b11 */
    g_assert_cmpuint(strength,==,STRENGTH_WEAK);

    /* Bring driver1 to hi-z and driver2 to normal: <1z> */
    qemu_wire_multi_drive((struct qemu_wire_drive[]) {      /* #3 */
        { .driver = driver1,
          .strength = STRENGTH_HI_Z },  /* 1w -> z */
        { .driver = driver2,
          .value = 1,                   /* z  -> 1d */
          .strength = STRENGTH_DEFAULT,
          .value_mode = VALUE_MODE_D },
    }, 2);

    /*
     * wire0: z         <- z
     * wire1: 1d        <- z + 1d
     * wirem: <1z>
     */
    g_assert(WIRE_IS_HI_Z(wire0));
    g_assert(!WIRE_IS_HI_Z(wire1));
    g_assert(qemu_wire_sense(wire1, NULL));
    (void) qemu_wire_multi_sense((qemu_wire[]){wire0, wire1}, 2, &strength);
    g_assert_cmpuint(strength,==,STRENGTH_HI_Z);

    g_assert_cmpstr(buf0,==,"1wz");             /*  1w  1w  zz */
    g_assert_cmpstr(buf1,==,"01w");             /* 0 s 1 w 1 d */
    g_assert_cmpstr(bufm,==,"<01>w<11>w<1z>z"); /* 01w 11w 1zz */

    /* cleanup */
    qemu_wire_multi_unlisten(token);
    qemu_wire_unlisten(wire1, recorder_handler, buf1);
    qemu_wire_unlisten(wire0, recorder_handler, buf0);
    qemu_free_wiredriver(driver2);
    qemu_free_wiredriver(driver1);
    qemu_free_wire(wire1);
    qemu_free_wire(wire0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);

    g_test_add_func("/wire/digital", test_wire_digital);
    g_test_add_func("/wire/analog", test_wire_analog);
    g_test_add_func("/wire/mixed", test_wire_mixed);
    g_test_add_func("/wire/listen", test_wire_listen);
    g_test_add_func("/wire/null", test_wire_null);
    g_test_add_func("/wire/multi", test_wire_multi);

    g_test_run();

    return 0;
}
