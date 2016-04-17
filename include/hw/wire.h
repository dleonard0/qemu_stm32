/*
 * Virtual wires API.
 *
 * A virtual wire holds the value most strongly asserted by an
 * attached wire-driver.
 *
 * Wire change listeners can be registered and are called:
 *   - when the value of a wire changes, or
 *   - when the wire becomes undriven (falls to Hi-Z), or
 *   - when the wire enters or leaves a driver conflict state.
 *
 * Each wire driver specifies a strength from 0 to 7. The
 * strongest driver of a wire sets the wire's value. Wire drivers
 * can attach to multiple wires without the cross-interference.
 * For example, you can use a single wire driver as a weak pull-up for
 * multiple wires.
 *
 * Drivers are operated in digital or analogue mode. In analogue mode
 * a driver drives the wire to a signed integer value (microvolts).
 * A driver in digital mode drives the wire to a boolean value.
 * The strongest driver attached to the wire determines both the wire's
 * value and the wire's mode, digital or analogue.
 *
 * A driver conflict occurs on a wire when equal-strongest drivers
 * disagree on a value or value mode. When a wire is in conflict,
 * the sensed value is not defined.
 *
 * Mixing of analogue and digital driving and sensing is supported and
 * works as follows:
 *   - A wire driven to an analogue value equal to or larger than half
 *     the wire's "intrinsic value" will sense as digital true. Below that
 *     and it will sense as digital false.
 *   - A wire driven to digital true will sense as an analogue
 *     value equal to the wire's intrinsic value. When the wire is
 *     driven to digital false it will sense as analogue zero.
 *
 * A multi-driver API are provided for dealing coherently with groups
 * of wire drivers. This can improve performance because changing
 * wire driver's state one-at-a-time can be slow and invoke needless
 * callbacks. Sensing a wire's level is always fast because it is
 * cached in the qemu_wire object.
 *
 * In all functions, a NULL qemu_wire can be supplied for a wire that always
 * senses Hi-Z or trivially sinks driver operations.
 */

#define TYPE_WIRE        "wire"
#define TYPE_WIREDRIVER  "wiredriver"

#define STRENGTH_HI_Z   0
#define STRENGTH_SMALL  1
#define STRENGTH_MEDIUM 2
#define STRENGTH_WEAK   3
#define STRENGTH_LARGE  4
#define STRENGTH_PULL   5
#define STRENGTH_STRONG 6
#define STRENGTH_SUPPLY 7

#define STRENGTH_DEFAULT                STRENGTH_PULL
#define STRENGTH_MAX                    STRENGTH_SUPPLY
#define WIRE_INTRINSIC_DEFAULT          3300000         /* 3.3e6 uV */

typedef struct WireState *qemu_wire;
typedef struct WireDriverState *qemu_wiredriver;
typedef void (*qemu_wire_handler)(void *opaque, qemu_wire wire);
typedef void (*qemu_wire_multi_handler)(void *opaque, uint32_t value,
                                        unsigned weakest_strength,
                                        unsigned n, const qemu_wire *wires);

#define WIRE_IS_HI_Z(wire) (qemu_wire_sense_strength(wire) == STRENGTH_HI_Z)

/*
 * Allocates a new virtual wire.
 * The wire will have an intrinsic analogue value of WIRE_INTRINSIC_DEFAULT
 */
qemu_wire qemu_allocate_wire(void);

/*
 * Releases an allocated wire.
 * Detaches all attached wire drivers and then unrefs the wire.
 */
void qemu_free_wire(qemu_wire wire);

/*
 * Allocates a new wire driver.
 * A new wire driver's initial strength is STRENGTH_HI_Z.
 * @wire   (optional) the wire to attach the driver to.
 */
qemu_wiredriver qemu_allocate_wiredriver(qemu_wire);

/*
 * Releases a wire driver.
 * Detaches the driver from all wires it is attached to,
 * then unrefs it.
 */
void qemu_free_wiredriver(qemu_wiredriver);

/*
 * Attaches a wire driver to a wire.
 * When attached, the wire holds a reference to the wire driver.
 * Wires automatically detach their drivers during finalization.
 * @wire     (optional) the wire to attach to
 * @driver   the driver to attach to the wire
 */
void qemu_wire_attach(qemu_wire wire, qemu_wiredriver driver);

/*
 * Detaches a previously attach driver from a wire.
 * This has no effect if the wire was not attached to the driver.
 * @wire     (optional) the wire to attach to
 * @driver   the driver to attach to the wire
 */
void qemu_wire_detach(qemu_wire wire, qemu_wiredriver driver);

/*
 * Sets a wire's intrinsic value.
 * The intrinsic value is used when reading digitally driven wires
 * for an analogue value.
 */
void qemu_set_wire_intrinsic(qemu_wire wire, int v);

/*
 * Sets the driver's digital output.
 * @strength  the drive strength, usually STRENGTH_DEFAULT
 * @dval      the driven digital value
 */
void qemu_wire_drive(qemu_wiredriver driver, unsigned strength, bool dval);

/*
 * Sets the driver's analogue output.
 * @strength  the drive strength, usually STRENGTH_DEFAULT
 * @aval      the driven analogue value
 */
void qemu_wire_drive_a(qemu_wiredriver driver, unsigned strength, int aval);

/*
 * Sets the wire driver's output to Hi-Z.
 */
void qemu_wire_drive_z(qemu_wiredriver driver);

/* Used for makeing multiple drive requests */
struct qemu_wire_drive {
    qemu_wiredriver driver;
    int value;                  /* ignored when strength is Hi-Z,
                                 * in digital mode must be 0 or 1 */
    unsigned strength   : 3,    /* STRENGTH_HI_Z .. STRENGTH_MAX */
             value_mode : 1;    /* ignored when strength is Hi-Z */
#define VALUE_MODE_D    0       /* digital value */
#define VALUE_MODE_A    1       /* analogue value */
};

/*
 * Update multiple wire drivers, coherently.
 * @drives   an array of drive descriptors
 * @n        length of the drives arrays
 */
void qemu_wire_multi_drive(const struct qemu_wire_drive *drives, unsigned n);

/*
 * Returns the digital value of the wire as determined by the strongest
 * attached wire driver.
 *
 * A NULL wire may be given, and is treated as an unattached Hi-Z wire.
 *
 * @wire            (optional) the wire to sense
 * @strength_return (optional) storage for holding the drive strength
 */
bool qemu_wire_sense(qemu_wire wire, unsigned *strength_return);

/*
 * Returns the combined digital values of a set of wires.
 * The first wire's value is returned in bit 0.
 * If any of the wires are Hi-Z their corresponding bit is undefined.
 *
 * @wires                      array of wires (some may be NULL)
 * @n                          length of the wires array
 * @weakest_strength_return    (optional) weakest wire strength return
 */
uint32_t qemu_wire_multi_sense(const qemu_wire *wires, unsigned n,
       unsigned *weakest_strength_return);

/*
 * Returns the analogue value of the wire as determined by the strongest
 * attached wire driver.
 *
 * @wire            (optional) the wire to sense
 * @strength_return (optional) storage for holding the drive strength
 */
int qemu_wire_sense_a(qemu_wire wire, unsigned *strength_return);

/*
 * Returns the strength of the strongest driver attached to the wire.
 * If no drivers are attached to the wire, or the wire is NULL, this
 * function returns STRENGTH_HI_Z.
 * @wire  (optional) wire
 */
unsigned qemu_wire_sense_strength(qemu_wire wire);

/*
 * Tests if the wire is in conflict.
 * Conflict occurs when the wire has drivers of equal strength that
 * disagree on the wire's value.
 * @wire  (optional) wire
 */
bool qemu_wire_sense_conflicted(qemu_wire wire);


/*
 * Adds a listener (handler) to a wire.
 * The listener will be called on the next value change of the wire.
 * @wire     (optional) the wire to listen to
 * @handler  the callback function to call on changes to the wire
 * @opaque   the callback function closure
 */
void qemu_wire_listen(qemu_wire wire, qemu_wire_handler handler, void *opaque);

/*
 * Removes a previously added listener (handler) from a wire.
 * @wire     (optoinal) the wire to stop listening to
 * @handler  the same handler function as given to qemu_wire_listen()
 * @opaque   the same closure pointer as given to qemu_wire_listen()
 */
void qemu_wire_unlisten(qemu_wire wire, qemu_wire_handler handler, void *opaque);

/*
 * Adds a listener that is called when the combined value of the wires
 * change.
 *
 * A token is returned that should be used for unlistening.
 * Returns NULL if n is zero.
 *
 * The handler is only called when one of the following is true:
 *      - the digital value of a wire changes, and none are Hi-Z
 *      - one of the wires goes Hi-Z when none previously were
 *      - none of the wires are Hi-Z when previously at least one was
 *      - a conflict appears, or all conflicts disappear
 */
void *qemu_wire_multi_listen(const qemu_wire *wires, unsigned n,
        qemu_wire_multi_handler multi_handler, void *opaque);

void qemu_wire_multi_unlisten(void *token);


/* IRQ helpers */
typedef struct IRQState *qemu_irq;

/*
 * An IRQ handler that converts IRQ changes into wire changes.
 * The IRQ level is converted into a digital assertion of
 * strength STRENGTH_DEFAULT.
 *
 * @opaque    a value of type qemu_wiredriver
 * @n         the IRQ's number
 * @level     digital level
 */
void qemu_wire_driver_irq_handler(void *opaque, int n, int level);

/*
 * Convenience function to allocates an IRQ that will control
 * a wire driver.
 *
 * The returned IRQ will act as a proxy for the wire.
 * The caller should initialize the IRQ state on return.
 *
 * @driver  the driver controlled by the IRQ
 * @n       the IRQ number
 */
qemu_irq qemu_wire_driver_irq(qemu_wiredriver driver, int n);

/*
 * A wire listener that converts wire changes into IRQ changes.
 *
 * This is a wire change handler that can drive an IRQ to the
 * same state as the wire. The opaque argument should be
 * a pointer to storage holding a qemu_irq, or holding NULL.
 * When the wire changes, state, the qemu_irq will be driven
 * to the same state.
 *
 * @opaque    a pointer of type qemu_irq*
 * @wire      the wire that changes
 */
void qemu_wire_irq_handler(void *opaque, qemu_wire wire);

/*
 * Convenience function to link up a wire to drive an IRQ.
 *
 * @wire    the wire that will drive the irq
 * @irqp    location holding a qemu_irq. It will be read when
 *          the wire changes state.
 */
void qemu_wire_listen_irq(qemu_wire wire, const qemu_irq *irqp);
void qemu_wire_unlisten_irq(qemu_wire wire, const qemu_irq *irqp);
