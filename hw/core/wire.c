/* vim: set ts=4 sw=4:
 * Virtual wires
 *
 * David Leonard, 2016. Released to public domain.
 */

#include "qemu-common.h"
#include "hw/wire.h"
#include "hw/irq.h"
#include "qemu/vector.h"
#include "qom/object.h"

#define WIRE(obj) OBJECT_CHECK(struct WireState, (obj), TYPE_WIRE)
#define WIREDRIVER(obj) OBJECT_CHECK(struct WireDriverState, (obj), TYPE_WIREDRIVER)

typedef struct WireState       WireState;
typedef struct WireDriverState WireDriverState;

/*------------------------------------------------------------
 * WireState
 */

struct wire_listener {
    qemu_wire_handler handler;
    void *opaque;
};

struct wire_driver_attachment {
    WireDriverState *driver;
};

struct WireState {
    struct Object object;

    int intrinsic;
    VECTOR_OF(struct wire_driver_attachment) attachments;
    VECTOR_OF(struct wire_listener) listeners;

    int value;                      /* set by wire_update() */
    unsigned strength : 3,          /* set by wire_update() */
             value_mode : 1,        /* set by wire_update() */
             is_conflict : 1,       /* set by wire_update() */
             changed : 1,           /* set by wire_update(), and
                                       wire_notify_if_changed() */
             in_callback : 1,       /* set by wire_call_listeners() */
             driver_changed : 1;    /* set by qemu_wire_multi_drive() */
};
static Type wire_type;

/* Remove all attachments and listeners from the wire */
static void wire_clear(WireState *wire)
{
    struct wire_driver_attachment *attach;
    struct wire_listener *lis;

    while (VECTOR_LEN(wire->attachments)) {
        attach = &VECTOR_LAST(wire->attachments);
        qemu_wire_detach(wire, attach->driver);
    }
    while (VECTOR_LEN(wire->listeners)) {
        lis = &VECTOR_LAST(wire->listeners);
        qemu_wire_unlisten(wire, lis->handler, lis->opaque);
    }
}

static void wire_instance_finalize(Object *obj)
{
    WireState *wire = (WireState *)obj;

    wire_clear(wire);
    VECTOR_FREE(wire->attachments);
    VECTOR_FREE(wire->listeners);
}

struct TypeInfo wire_info = {
    .name = TYPE_WIRE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof (WireState),
    .instance_finalize = wire_instance_finalize,
};

void qemu_wire_listen(qemu_wire wire, qemu_wire_handler handler, void *opaque)
{
    const struct wire_listener lis = { handler, opaque };

    if (!wire)
        return;
    VECTOR_APPEND(wire->listeners, lis);
}

void qemu_wire_unlisten(qemu_wire wire, qemu_wire_handler handler, void *opaque)
{
    unsigned i;
    const struct wire_listener *lis;

    if (!wire)
        return;

    /* Search backwards since most likely it was last added */
    for (i = VECTOR_LEN(wire->listeners); i--; ) {
        lis = &VECTOR_AT(wire->listeners, i);
        if (lis->handler == handler && lis->opaque == opaque) {
            VECTOR_DEL(wire->listeners, i);
            break;
        }
    }
}

/* Notifies all listeners of a change */
static void wire_call_listeners(WireState *wire)
{
    unsigned i;
    const struct wire_listener *listener;

    if (wire->in_callback) {
        fprintf(stderr, "%s: wire callback altered wire\n",
            object_get_canonical_path(OBJECT(wire)));
    }
    wire->in_callback = 1;

    /* Iterate backwards so that a listener can unregister
     * itself if needed */
    for (i = VECTOR_LEN(wire->listeners); i--; ) {
        listener = &VECTOR_AT(wire->listeners, i);
        listener->handler(listener->opaque, wire);
    }

    wire->in_callback = 0;
}

static void wire_notify_if_changed(qemu_wire wire)
{
    if (wire->changed) {
        wire->changed = 0;
        wire_call_listeners(wire);
    }
}

/*------------------------------------------------------------
* WireDriverState
*/

struct WireDriverState {
    struct Object object;
    VECTOR_OF(WireState *) wires;
    int value;
    unsigned strength : 3,
             value_mode : 1,
             changed : 1;
};
static Type wiredriver_type;

static void wiredriver_clear(WireDriverState *driver)
{
    unsigned i;

    for (i = VECTOR_LEN(driver->wires); i--; ) {
        qemu_wire_detach(VECTOR_AT(driver->wires, i), driver);
    }
}

static void wiredriver_instance_finalize(Object *obj)
{
    WireDriverState *driver = (WireDriverState *)obj;

    wiredriver_clear(driver);
    VECTOR_FREE(driver->wires);
}

struct TypeInfo wiredriver_info = {
    .name = TYPE_WIREDRIVER,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof (WireDriverState),
    .instance_finalize = wiredriver_instance_finalize,
};

/*
 * Updates the wire's value by searching the
 * attached drivers for the strongest signal.
 *
 * This function also sets wire->changed if the value
 * or value_mode changes, or if it enters or leaves a conflict mode.
 *
 * See also: wire_notify_if_changed() which clears wire->changed.
 */
static void wire_update(WireState *wire)
{
    unsigned i;
    struct WireDriverState *driver;
    struct qemu_wire_drive best = {
        .driver = NULL,
        .value = 0,
        .strength = STRENGTH_HI_Z,
        .value_mode = VALUE_MODE_D,
    };
    bool is_conflict = false;

    /* Walk the drivers, updating our 'best' value */
    for (i = VECTOR_LEN(wire->attachments); i--; ) {
        driver = VECTOR_AT(wire->attachments, i).driver;
        if (!driver->strength || best.strength > driver->strength)
            continue;
        if (driver->strength == best.strength) {
            if (is_conflict)
                continue;   /* already conflicting: skip */
            if (best.value_mode != driver->value_mode)
                is_conflict = true; /* conflict by value mode */
            else if (best.value != driver->value)
                is_conflict = true; /* conflict by value */
            continue;
        }
        /* Found new strongest driver */
        best.strength = driver->strength;
        best.value_mode = driver->value_mode;
        best.value = driver->value;
        /* best.driver = driver; (unused) */
        is_conflict = false;
    }

    /* Is the best value different to the current value? */
    if (!wire->changed)
        wire->changed =
           (is_conflict != wire->is_conflict) || /* changing conflict */
           (!best.strength && wire->strength) || /* going Hi-Z */
           (best.strength && !wire->strength) || /* leaving Hi-Z */
           (best.strength && (
             (best.value_mode != wire->value_mode) ||
             (best.value != wire->value)         /* non Hi-Z value change */
           ));

    wire->strength = best.strength;
    wire->is_conflict = is_conflict;
    wire->value = best.value;
    wire->value_mode = best.value_mode;
}

/*------------------------------------------------------------
 * public interface
 */

qemu_wire qemu_allocate_wire(void)
{
    WireState *wire;

    wire = WIRE(object_new_with_type(wire_type));
    if (wire) {
        wire->intrinsic = WIRE_INTRINSIC_DEFAULT;
    }
    return wire;
}

void qemu_free_wire(qemu_wire wire)
{
    if (wire) {
        wire_clear(wire);
        object_unref(&wire->object);
    }
}

qemu_wiredriver qemu_allocate_wiredriver(qemu_wire wire)
{
    qemu_wiredriver driver;

    driver = WIREDRIVER(object_new_with_type(wiredriver_type));
    if (wire) {
        qemu_wire_attach(wire, driver);
    }
    return driver;
}

void qemu_free_wiredriver(qemu_wiredriver driver)
{
    if (driver) {
        wiredriver_clear(driver);
        object_unref(&driver->object);
    }
}

void qemu_wire_attach(qemu_wire wire, qemu_wiredriver driver)
{
    const struct wire_driver_attachment at = {
        .driver = driver
    };
    if (!wire)
        return;
    VECTOR_APPEND(wire->attachments, at);
    VECTOR_APPEND(driver->wires, wire);
    object_ref(&driver->object);
}

void qemu_wire_detach(qemu_wire wire, qemu_wiredriver driver)
{
    unsigned i;

    if (!wire)
        return;

    for (i = VECTOR_LEN(driver->wires); i--; ) {
        if (VECTOR_AT(driver->wires, i) == wire) {
            VECTOR_DEL(driver->wires, i);
            break;
        }
    }

    for (i = VECTOR_LEN(wire->attachments); i--; ) {
        if (VECTOR_AT(wire->attachments, i).driver == driver) {
            VECTOR_DEL(wire->attachments, i);
            object_unref(&driver->object);
            break;
        }
    }

    wire_update(wire);
    wire_notify_if_changed(wire);
}

void qemu_set_wire_intrinsic(qemu_wire wire, int v)
{
    if (wire) {
        wire->intrinsic = v;
    }
}

void qemu_wire_drive_z(qemu_wiredriver driver)
{
    qemu_wire_drive(driver, STRENGTH_HI_Z, false);
}

void qemu_wire_drive(qemu_wiredriver driver, unsigned strength, bool dval)
{
    struct qemu_wire_drive wiredrive = {
        .driver = driver,
        .value = dval ? 1: 0,
        .strength = strength,
        .value_mode = VALUE_MODE_D,
    };
    qemu_wire_multi_drive(&wiredrive, 1);
}

void qemu_wire_drive_a(qemu_wiredriver driver, unsigned strength, int aval)
{
    struct qemu_wire_drive wiredrive = {
        .driver = driver,
        .value = aval,
        .strength = strength,
        .value_mode = VALUE_MODE_A,
    };
    qemu_wire_multi_drive(&wiredrive, 1);
}

void qemu_wire_multi_drive(const struct qemu_wire_drive *drives, unsigned n)
{
    const struct qemu_wire_drive *wd;
    qemu_wiredriver driver;
    qemu_wire wire;
    unsigned i;

    /* Copy the new values into their drivers */
    for (wd = drives; wd < drives + n; wd++) {
        if (!(driver = wd->driver))
            continue;
        if (driver->strength == wd->strength &&
                driver->value_mode == wd->value_mode &&
                driver->value == wd->value)
            continue;

        /* assert(!driver->changed); */

        driver->strength = wd->strength;
        driver->value_mode = wd->value_mode;
        driver->value = wd->value;
        driver->changed = 1;
        for (i = 0; i < VECTOR_LEN(driver->wires); i++) {
            wire = VECTOR_AT(driver->wires, i);
            wire->driver_changed = 1;
        }
    }

    /* Update each wire that has a driver that changed. */
    for (wd = drives; wd < drives + n; wd++) {
        if (!(driver = wd->driver))
            continue;
        if (!driver->changed)
            continue;
        for (i = 0; i < VECTOR_LEN(driver->wires); i++) {
            wire = VECTOR_AT(driver->wires, i);
            if (wire->driver_changed) {
                wire_update(wire);  /* can set wire->changed */
                wire->driver_changed = 0; /* no need to re-update later */
            }
        }
    }

    /* Now call all the listeners that may have changed */
    for (wd = drives; wd < drives + n; wd++) {
        if (!(driver = wd->driver))
            continue;
        if (!driver->changed)
            continue;
        driver->changed = 0;
        for (i = 0; i < VECTOR_LEN(driver->wires); i++) {
            wire = VECTOR_AT(driver->wires, i);
            wire_notify_if_changed(wire);
        }
    }
}

bool qemu_wire_sense(qemu_wire wire, unsigned *strength_return)
{
    bool value = false;
    unsigned strength = STRENGTH_HI_Z;

    if (wire) {
        switch (wire->value_mode) {
        case VALUE_MODE_A:
            value = wire->value >= wire->intrinsic / 2;
            break;
        case VALUE_MODE_D:
            value = wire->value;
            break;
        }
        strength = wire->strength;
    }
    if (strength_return)
        *strength_return = strength;
    return value;
}

int qemu_wire_sense_a(qemu_wire wire, unsigned *strength_return)
{
    int value = 0;
    unsigned strength = STRENGTH_HI_Z;

    if (wire) {
        switch (wire->value_mode) {
        case VALUE_MODE_A:
            value = wire->value;
            break;
        case VALUE_MODE_D:
            value = wire->value ? wire->intrinsic : 0;
            break;
        }
        strength = wire->strength;
    }
    if (strength_return)
        *strength_return = strength;
    return value;
}

uint32_t qemu_wire_multi_sense(const qemu_wire *wires, unsigned n,
       unsigned *weakest_strength_return)
{
    unsigned weakest = STRENGTH_HI_Z;
    unsigned strength;
    uint32_t result = 0, bit = 1;
    unsigned i;

    if (n > 32)
        n = 32;
    for (bit = 1, i = 0; i < n; bit <<= 1, i++) {
        if (qemu_wire_sense(wires[i], &strength))
            result |= bit;
        if (i == 0 || strength < weakest)
            weakest = strength;
    }
    if (weakest_strength_return)
        *weakest_strength_return = weakest;
    return result;
}

unsigned qemu_wire_sense_strength(qemu_wire wire)
{
    return wire ? wire->strength : STRENGTH_HI_Z;
}

bool qemu_wire_sense_conflicted(qemu_wire wire)
{
    return wire ? wire->is_conflict : false;
}


struct multi_listener {
    qemu_wire_multi_handler handler;
    void *opaque;
    unsigned n;
    uint32_t value;
    unsigned weakest_strength;
    unsigned in_conflict : 1;
    qemu_wire wires[];
};

static void multi_handler(void *opaque, qemu_wire wire)
{
    struct multi_listener *ml = opaque;
    unsigned i;
    bool in_conflict;
    unsigned weakest_strength;
    uint32_t value;
    bool changed;

    in_conflict = false;
    for (i = ml->n; !in_conflict && i--;)
        in_conflict = ml->wires[i] && ml->wires[i]->is_conflict;
    if (in_conflict && ml->in_conflict)
        return; /* don't update while wires remain in conflict */

    value = qemu_wire_multi_sense(ml->wires, ml-> n, &weakest_strength);
    changed = (in_conflict != ml->in_conflict) ||
              (!weakest_strength && ml->weakest_strength) ||
              (weakest_strength && !ml->weakest_strength) ||
              (weakest_strength && (value != ml->value || ml->n > 32));
    if (changed) {
        ml->weakest_strength = weakest_strength;
        ml->value = value;
        ml->in_conflict = in_conflict;
        ml->handler(ml->opaque, value, weakest_strength, ml->n, ml->wires);
    }
}

void *qemu_wire_multi_listen(const qemu_wire *wires, unsigned n,
        qemu_wire_multi_handler handler, void *opaque)
{
    unsigned i;
    struct multi_listener *ml;

    if (n == 0) {
        return NULL;
    }
    ml = g_malloc(sizeof *ml + n * sizeof ml->wires[0]);

    if (ml) {
        ml->handler = handler;
        ml->opaque = opaque;
        ml->weakest_strength = STRENGTH_HI_Z;
        ml->in_conflict = 0;
        ml->n = n;
        for (i = 0; i < n; i++) {
            ml->wires[i] = wires[i];
            qemu_wire_listen(wires[i], multi_handler, ml);
        }
    }
    return ml;
}

void qemu_wire_multi_unlisten(void *token)
{
    unsigned i;
    struct multi_listener *ml = token;

    if (ml) {
        for (i = 0; i < ml->n; i++) {
            qemu_wire_unlisten(ml->wires[i], multi_handler, ml);
        }
    }
}

static void wire_register_types(void)
{
    wire_type = type_register_static(&wire_info);
    wiredriver_type = type_register_static(&wiredriver_info);
}
type_init(wire_register_types)

/*------------------------------------------------------------
 * IRQ helpers
 */

/* irq->wire */
void qemu_wire_driver_irq_handler(void *opaque, int n, int level)
{
    qemu_wiredriver driver = opaque;
    qemu_wire_drive(driver, STRENGTH_DEFAULT, level ? true : false);
}

qemu_irq qemu_wire_driver_irq(qemu_wiredriver driver, int n)
{
    return qemu_allocate_irq(qemu_wire_driver_irq_handler, driver, n);
}


/* wire->irq */
void qemu_wire_irq_handler(void *opaque, qemu_wire wire)
{
    const qemu_irq *irqp = opaque;

    if (!*irqp) {
        /* ignore */
    } else if (WIRE_IS_HI_Z(wire)) {
        fprintf(stderr, "irq %p's wire %p is hi-Z\n", *irqp, wire);
    } else {
        qemu_set_irq(*irqp, qemu_wire_sense(wire, NULL));
    }
}

void qemu_wire_listen_irq(qemu_wire wire, const qemu_irq *irqp)
{
    qemu_wire_listen(wire, qemu_wire_irq_handler, (void *)irqp);
}

void qemu_wire_unlisten_irq(qemu_wire wire, const qemu_irq *irqp)
{
    qemu_wire_unlisten(wire, qemu_wire_irq_handler, (void *)irqp);
}

