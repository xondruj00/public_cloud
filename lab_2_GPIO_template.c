/*
 * ============================================================
 * IMP  –  Lab 2  (SABLONA PRO STUDENTY)
 * GPIO vyvody, preruseni, 4ciselny 7+1segmentovy LED displej
 * ============================================================
 *
 * Student/autor ID
 * ----------------
 * jmeno a prijmeni:
 * login na FIT VUT : xondruj00
 *
 * ============================================================
 * Cilovy MCU : MCXC143VFT
 *
 * Vyuzite vyvody (piny)
 * ---------------------
 * Pripojeni vyvodu displeje LTC-4627G:
 * - spolecne anody pro DIGx, ovladani pres P-FET (LOW = P-FET ON -> HIGH na DIGx),
 * - segmenty aktivni v LOW, ovladani pres SEG_x
 * ---------------------------------------------------------------
 * PTE18 = SEG_A    PTE19 = SEG_B    PTE20 = SEG_C    PTE21 = SEG_D
 * PTE24 = SEG_E    PTE25 = SEG_F    PTE29 = SEG_G    PTE30 = SEG_DP
 * ---------------------------------------------------------------
 * Vyvody pro vyber cislic:
 * PTA1 = DIG1_SEL   PTA2 = DIG2_SEL   PTA4 = DIG3_SEL
 * PTC2 = DIG4_SEL
 *
 * Vyvody pro tlacitka:
 * - nestisknuto/OFF = nedef., stisknuto/ON = LOW, 
 * ( -> pouzit pull-up, preruseni citlive na sestupnou hranu)
 * PTC1 = PB1 (UP)       PTC3 = PB2 (LEFT)     PTC4 = PB3 (CENTER)
 * PTC5 = PB4 (RIGHT)    PTC6 = PB5 (DOWN)
 *
 * ============================================================
 * Obrazove vzory pro [DIG1] [DIG2] [DIG3] [DIG4]
 * ============================================================
 * ┌──────────────────┬──────┬──────┬──────┬──────┐
 * │ Vzor             │ DIG1 │ DIG2 │ DIG3 │ DIG4 │
 * ├──────────────────┼──────┼──────┼──────┼──────┤
 * │ FACE_DEFAULT     │  o   │  _   │  _   │  o   │
 * │ FACE_SURPRISED   │  O   │  _   │  _   │  O   │
 * │ FACE_ASLEEP      │  _   │  -   │  -   │  _   │
 * │ FACE_WINK_LEFT   │  ^   │  _   │  _   │  o   │
 * │ FACE_WINK_RIGHT  │  o   │  _   │  _   │  ^   │
 * │ FACE_HAPPY       │  U   │  _   │  _   │  U   │
 * └──────────────────┴──────┴──────┴──────┴──────┘
 * ============================================================
 */

#include "MCXC143.h"

/* ── Identifikatory tlacitek ─────────────────────────────────────────────── */

#define PTB1_LEDR	1
#define PTB1_LEDG	2
#define PTB2_LEDR	4
#define PTB2_LEDG	8
#define PTB3_LEDR	16
#define PTB3_LEDG	32
#define PTB4_LEDR	64
#define PTB4_LEDG	128
#define PTB5_LEDR	256
#define PTB5_LEDG	512

/* Vyvody tlacitek na PORTC */
#define BTN_UP      1u    /* PTC1 = PB1 */
#define BTN_LEFT    3u    /* PTC3 = PB2 */
#define BTN_CENTER  4u    /* PTC4 = PB3 */
#define BTN_RIGHT   5u    /* PTC5 = PB4 */
#define BTN_DOWN    6u    /* PTC6 = PB5 */
#define BTN_MASK    ((1u << BTN_UP) | (1u << BTN_LEFT) | (1u << BTN_CENTER) | \
                     (1u << BTN_RIGHT) | (1u << BTN_DOWN))

/* ── Konstanty pro displej ───────────────────────────────────────────────── */

#define SEG_A   (1u << 18)
#define SEG_B   (1u << 19)
#define SEG_C   (1u << 20)
#define SEG_D   (1u << 21)
#define SEG_E   (1u << 24)
#define SEG_F   (1u << 25)
#define SEG_G   (1u << 29)
#define SEG_DP  (1u << 30)
#define ALL_SEGS (SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F|SEG_G|SEG_DP)

/* Masky pro vyber cislic */
#define DIG1  (1u << 1)   /* GPIOA PTA1 */
#define DIG2  (1u << 2)   /* GPIOA PTA2 */
#define DIG3  (1u << 4)   /* GPIOA PTA4 */
#define DIG4  (1u << 2)   /* GPIOC PTC2 */

/* Definice vzoru */
#define PAT_o      (SEG_C | SEG_D | SEG_E | SEG_G)
#define PAT_O      (SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F)
#define PAT_U      (SEG_B | SEG_C | SEG_D | SEG_E | SEG_F)
#define PAT_LINE_D (SEG_D)
#define PAT_LINE_G (SEG_G)
#define PAT_WINK   (SEG_C | SEG_E | SEG_G)  

/* Vzor pro uvitani */
#define PATTERN_L  (SEG_D | SEG_E | SEG_F)
#define PATTERN_A  (SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G)
#define PATTERN_b  (SEG_C | SEG_D | SEG_E | SEG_F | SEG_G)
#define PATTERN_2  (SEG_A | SEG_B | SEG_D | SEG_E | SEG_G)

/* Vycet vzoru */
typedef enum {
    FACE_DEFAULT,
    FACE_SURPRISED,
    FACE_ASLEEP,
    FACE_WINK_LEFT,
    FACE_WINK_RIGHT,
    FACE_HAPPY
} face_state_t;

/* ── Stav aplikace ───────────────────────────────────────────────────────── */

static volatile uint32_t face[4];

/*
 * face_ticks: dekrementuje se s kazdym cyklem prekresleni displeje v main().
 * - nastavi se na FACE_TIMEOUT v ISR po stisku tlacitka.
 * - po dosazeni 0 se vzor vrati na vychozi.
 */
#define FACE_TIMEOUT  600u
static volatile uint32_t face_ticks = 0;

/* ── Pomocne funkce ──────────────────────────────────────────────────────── */

/* SysTick delay – 48 MHz core clock, 24-bit counter max ~349 ms */
#define SYSTICK_MAX_US 349000UL
static void delay_us(uint32_t us) {
    if (!us) return;
    if (us > SYSTICK_MAX_US) us = SYSTICK_MAX_US;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    SysTick->CTRL = 0; SysTick->VAL = 0;
    SysTick->LOAD = 48UL * us - 1UL;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)) {}
    SysTick->CTRL = 0;
    __set_PRIMASK(primask);
}
static void delay_ms(uint32_t ms) {
    uint32_t c = ms / 349; while (c--) delay_us(349000UL);
    uint32_t r = ms % 349; if (r) delay_us(r * 1000UL);
}

static void display_off(void)
{
    GPIOE->PSOR = ALL_SEGS;
    GPIOA->PSOR = DIG1 | DIG2 | DIG3;
    GPIOC->PSOR = DIG4;
}

static void show_on_dig123(uint32_t dig_porta_bit, uint32_t pattern)
{
    display_off();
    GPIOE->PCOR = pattern;
    GPIOA->PCOR = dig_porta_bit;
}

static void show_on_dig4(uint32_t pattern)
{
    display_off();
    GPIOE->PCOR = pattern;
    GPIOC->PCOR = DIG4;
}

static void set_face(face_state_t state)
{
    switch (state) {
        case FACE_DEFAULT:
            face[0] = PAT_o;      face[1] = PAT_LINE_D; face[2] = PAT_LINE_D; face[3] = PAT_o;
            face_ticks = 0;       /* pro vychozi vzor timeout neplati */
            break;
        case FACE_SURPRISED:
            face[0] = PAT_O;      face[1] = PAT_LINE_D; face[2] = PAT_LINE_D; face[3] = PAT_O;
            face_ticks = FACE_TIMEOUT;
            break;
        case FACE_ASLEEP:
            face[0] = PAT_LINE_D; face[1] = PAT_LINE_G; face[2] = PAT_LINE_G; face[3] = PAT_LINE_D;
            face_ticks = FACE_TIMEOUT;
            break;
        case FACE_WINK_LEFT:
            face[0] = PAT_WINK;   face[1] = PAT_LINE_D; face[2] = PAT_LINE_D; face[3] = PAT_o;
            face_ticks = FACE_TIMEOUT;
            break;
        case FACE_WINK_RIGHT:
            face[0] = PAT_o;      face[1] = PAT_LINE_D; face[2] = PAT_LINE_D; face[3] = PAT_WINK;
            face_ticks = FACE_TIMEOUT;
            break;
        case FACE_HAPPY:
            face[0] = PAT_U;      face[1] = PAT_LINE_D; face[2] = PAT_LINE_D; face[3] = PAT_U;
            face_ticks = FACE_TIMEOUT;
            break;
    }
}

/* ── Uvitaci animace – 3x skroluje "LAb2" ────────────────────────────────── */
static void welcome_animation(void)
{
    const uint32_t HOLD = 250000;
    int round;
    for (round = 0; round < 3; round++) {
        show_on_dig123(DIG1, PATTERN_L); delay_us(HOLD); display_off();
        show_on_dig123(DIG2, PATTERN_A); delay_us(HOLD); display_off();
        show_on_dig123(DIG3, PATTERN_b); delay_us(HOLD); display_off();
        show_on_dig4(PATTERN_2);         delay_us(HOLD); display_off();
    }
}

/* ── Obsluha preruseni od tlacitek ───────────────────────────────────────── */
static void buttons_ledR_on(uint32_t btnled);
static void buttons_ledR_off(uint32_t btnled);
static void buttons_ledG_on(uint32_t btnled);
static void buttons_ledG_off(uint32_t btnled);

/*
 * Vraci 1, pokud ma vyvod nastaveny ISF a tlacitko je po debounce stale
 * stisknuto (LOW). ISF vyvodu vynuluje (w1c). Zakmit pri uvolneni tlacitka
 * (pin uz je HIGH) se tak ignoruje.
 */
static int btn_pressed(uint32_t pin)
{
    if (!(PORTC->PCR[pin] & PORT_PCR_ISF_MASK)) return 0;
    PORTC->PCR[pin] |= PORT_PCR_ISF_MASK;          /* nulovani ISF zapisem 1 */
    return (GPIOC->PDIR & (1u << pin)) == 0;
}

void PORTC_PORTD_IRQHandler(void)
{
    /* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! *
     * UKOL 2: Obsluha preruseni od tlacitek                                  *
     * (pro jednoduchost nepredpokladejte stisk vice tlacitek najednou)       *
     *                                                                        *
     * 1. Z ISF (Interrupt Status Flag) priznaku v Pin Control Register (PCR) * 
     *    zjistete, ktery tlacitkovy vyvod vyvolal preruseni (jeho ISF je 1)  *
     * 2. Vynulujte ISF priznak tohoto vyvodu                                 *
     * 3. Reagujte na stisk tlacitka; varianty (lze kombinovat):              *
     *    - pomoci set_face() zobrazte unikatni vzor na LED displeji          *
     *    - pomoci buttons_ledR_on(), buttons_ledG_on() podsvitte stisknute   *
     *      tlacitko cervenou, zelenou ci smisenou barvou                     *
     *                                                                        *
     * Priklad pro tlacitko UP (PTC1):                                        *
     *   if (PORTC->PCR[1] & ...) {                                           *
     *       PORTC->PCR[1] |= ...;  // nulovani ISF pro PTC1                  *
     *       set_face(...);                                                   *
     *       
     *       buttons_ledR_on(PTB1_LEDR);
     *       delay_ms(20);
     *       buttons_ledR_off(PTB1_LEDR);
     *   }                                                                    *
     *                                                                        *
     * Priklad vzoru pro reakce na stisk                                      *
     *   - UP     (PTC1)  ->  FACE_SURPRISED                                  *
     *   - LEFT   (PTC3)  ->  FACE_WINK_LEFT                                  *
     *   - CENTER (PTC4)  ->  FACE_HAPPY                                      *
     *   - RIGHT  (PTC5)  ->  FACE_WINK_RIGHT                                 *
     *   - DOWN   (PTC6)  ->  FACE_ASLEEP                                     *
     * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */

      /* signalizace zacatku obsluhy */
    	buttons_ledR_on(PTB3_LEDR); delay_ms(20);


    // DOPLNTE: napiste obsluhu preruseni pro zvolene tlacitko; 
    // po overeni jeji funkcnosti doprogramujte obsluhu pro zbyla tlacitka

    if (btn_pressed(BTN_UP)) {
        set_face(FACE_SURPRISED);
        buttons_ledR_on(PTB1_LEDR); delay_ms(20); buttons_ledR_off(PTB1_LEDR);
    }
    else if (btn_pressed(BTN_LEFT)) {
        set_face(FACE_WINK_LEFT);
        buttons_ledG_on(PTB2_LEDG); delay_ms(20); buttons_ledG_off(PTB2_LEDG);
    }
    else if (btn_pressed(BTN_CENTER)) {
        set_face(FACE_HAPPY);
        buttons_ledG_on(PTB3_LEDG); delay_ms(20); buttons_ledG_off(PTB3_LEDG);
    }
    else if (btn_pressed(BTN_RIGHT)) {
        set_face(FACE_WINK_RIGHT);
        buttons_ledG_on(PTB4_LEDG); delay_ms(20); buttons_ledG_off(PTB4_LEDG);
    }
    else if (btn_pressed(BTN_DOWN)) {
        set_face(FACE_ASLEEP);
        buttons_ledR_on(PTB5_LEDR); buttons_ledG_on(PTB5_LEDG);   /* smisena barva */
        delay_ms(20);
        buttons_ledR_off(PTB5_LEDR); buttons_ledG_off(PTB5_LEDG);
    }

    /* zahod ISF nastavene zakmity behem obsluhy -> kazdy stisk obslouzen jednou */
    PORTC->ISFR = BTN_MASK;

      /* signalizace konce obsluhy */
    	buttons_ledR_off(PTB3_LEDR);
}

/* ── Inicializace HW ─────────────────────────────────────────────────────── */

static void clock_init(void)
{
    SIM->COPC = SIM_COPC_COPT(0);
    MCG->MC |= MCG_MC_HIRCEN_MASK;
    MCG->C1 = (uint8_t)(MCG->C1 & ~MCG_C1_CLKS_MASK) | MCG_C1_CLKS(0);
    while ((MCG->S & MCG_S_CLKST_MASK) != MCG_S_CLKST(0)) { }
    SIM->CLKDIV1 = SIM_CLKDIV1_OUTDIV1(0) | SIM_CLKDIV1_OUTDIV4(1);
}

static void display_init(void)
{
    PORTE->PCR[18] = PORT_PCR_MUX(1); PORTE->PCR[19] = PORT_PCR_MUX(1);
    PORTE->PCR[20] = PORT_PCR_MUX(1); PORTE->PCR[21] = PORT_PCR_MUX(1);
    PORTE->PCR[24] = PORT_PCR_MUX(1); PORTE->PCR[25] = PORT_PCR_MUX(1);
    PORTE->PCR[29] = PORT_PCR_MUX(1); PORTE->PCR[30] = PORT_PCR_MUX(1);
    GPIOE->PDDR   |= ALL_SEGS;
    GPIOE->PSOR    = ALL_SEGS;

    PORTA->PCR[1]  = PORT_PCR_MUX(1); PORTA->PCR[2]  = PORT_PCR_MUX(1);
    PORTA->PCR[4]  = PORT_PCR_MUX(1);
    GPIOA->PDDR   |= DIG1 | DIG2 | DIG3;
    GPIOA->PSOR    = DIG1 | DIG2 | DIG3;

    PORTC->PCR[2]  = PORT_PCR_MUX(1);
    GPIOC->PDDR   |= DIG4;
    GPIOC->PSOR    = DIG4;
}

static void buttons_init(void)
{
    /* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! *
     * UKOL 1: Spravne nakonfigurujte vyvody MCU pripojene k tlacitkum.       *
     *                                                                        *
     * 1) V Pin Control Register (PCR) pro kazdy vyvod nastavte:              *
     * - GPIO rezim (MUX na 1)                                                *
     * - pull rezistor (PE = 1) ve variante pull-up (PS = 1)                  *
     * - citlivost preruseni na setupnou hranu (IRQC = 1010b)                 *
     * Tip: konfiguracni hodnotu si ulozte do lokalni promenne, nasledne jeji * 
     * hodnotu priradte do kazdeho z PCR registru vyvodu tlacitek:            *
     *   uint32_t btn_pcr = ...;                                              *
     *   PORTC->PCR[x] = btn_pcr;                                             *
     *                                                                        *
     * 2) Vynulujte v NVIC pripadne nastavene ISF tlacitkovych vyvodu         * 
     *    (vyuzijte napr. makro NVIC_ClearPendingIRQ(X), popr. primo          *
     *    zapiste do NVIC->ICPR[Y]), pote povolte v NVIC preruseni pro        *
     *    tlacitkove vyvody (vyuzijte makro NVIC_EnableIRQ(X), popr. primo    *
     *    zapiste do NVIC->ISER[Y]).                                          *
     * Pozn.: - namisto X pouzijte spravne IRQ,                               *
     *        - namisto Y pouzijte spravny index registru                     *
     *                                                                        *
     * Naprogramujete-li vse spravne, pak stisk libovolneho z tlacitek        *
     * vyvola obsluhu preruseni PORTC_PORTD_IRQHandler(), jejiz provadeni     *
     * bude signalizovano cervenym podsvicenim prostredniho tlacitka (PB3).   *
     * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

    // DOPLNTE (1): 
    // Spravne nakonfigurujte PCR kazdeho tlacitkoveho vyvodu MCU 

    /* (1) GPIO, pull-up, preruseni na sestupnou hranu (IRQC = 1010b) */
    uint32_t btn_pcr = PORT_PCR_MUX(1)
                     | PORT_PCR_PE_MASK
                     | PORT_PCR_PS_MASK
                     | PORT_PCR_IRQC(0xA);

    PORTC->PCR[BTN_UP]     = btn_pcr;
    PORTC->PCR[BTN_LEFT]   = btn_pcr;
    PORTC->PCR[BTN_CENTER] = btn_pcr;
    PORTC->PCR[BTN_RIGHT]  = btn_pcr;
    PORTC->PCR[BTN_DOWN]   = btn_pcr;

    GPIOC->PDDR &= ~BTN_MASK;          /* vstupy (PTC2 = DIG4 zustava vystup) */
    PORTC->ISFR  = BTN_MASK;           /* vynuluj pripadne ISF z konfigurace */

    // DOPLNTE (2): 
    // Vynulujte v NVIC pripadne nastavene ISF tlacitkovych vyvodu 
    // Povolte v NVIC preruseni pro tlacitkove vyvody

    /* (2) NVIC: PORTC_PORTD = IRQ 31 -> ICPR[0]/ISER[0], bit 31 */
    NVIC_ClearPendingIRQ(PORTC_PORTD_IRQn);
    NVIC_EnableIRQ(PORTC_PORTD_IRQn);
}

static void buttons_ledR_on(uint32_t btnled)
{
	if(btnled & PTB1_LEDR) GPIOB->PDOR &= ~(1u << 1);
	if(btnled & PTB2_LEDR) GPIOB->PDOR &= ~(1u << 2);
	if(btnled & PTB3_LEDR) GPIOB->PDOR &= ~(1u << 3);
	if(btnled & PTB4_LEDR) GPIOB->PDOR &= ~(1u << 16);
	if(btnled & PTB5_LEDR) GPIOB->PDOR &= ~(1u << 17);
}

static void buttons_ledR_off(uint32_t btnled)
{
	if(btnled & PTB1_LEDR) GPIOB->PDOR |= (1u << 1);
	if(btnled & PTB2_LEDR) GPIOB->PDOR |= (1u << 2);
	if(btnled & PTB3_LEDR) GPIOB->PDOR |= (1u << 3);
	if(btnled & PTB4_LEDR) GPIOB->PDOR |= (1u << 16);
	if(btnled & PTB5_LEDR) GPIOB->PDOR |= (1u << 17);
}

static void buttons_ledG_on(uint32_t btnled)
{
	if(btnled & PTB1_LEDG) GPIOD->PDOR &= ~(1u << 3);
	if(btnled & PTB2_LEDG) GPIOD->PDOR &= ~(1u << 4);
	if(btnled & PTB3_LEDG) GPIOD->PDOR &= ~(1u << 5);
	if(btnled & PTB4_LEDG) GPIOD->PDOR &= ~(1u << 6);
	if(btnled & PTB5_LEDG) GPIOD->PDOR &= ~(1u << 7);
}

static void buttons_ledG_off(uint32_t btnled)
{
	if(btnled & PTB1_LEDG) GPIOD->PDOR |= (1u << 3);
	if(btnled & PTB2_LEDG) GPIOD->PDOR |= (1u << 4);
	if(btnled & PTB3_LEDG) GPIOD->PDOR |= (1u << 5);
	if(btnled & PTB4_LEDG) GPIOD->PDOR |= (1u << 6);
	if(btnled & PTB5_LEDG) GPIOD->PDOR |= (1u << 7);
}

static void buttons_led_init()
{
  /* pro ovladani LED pouzijeme GPIO vyvody (MUX1 -> PCR) */
	uint32_t btnled_pcr = PORT_PCR_MUX(1);

	/* nastav GPIO pro LEDxR */
	PORTB->PCR[1] = PORTB->PCR[2] = PORTB->PCR[3] = PORTB->PCR[16] = PORTB->PCR[17] = btnled_pcr;
	/* nastav na OUT rezim pro LEDxR */
	GPIOB->PDDR |= (1u << 1) | (1u << 2) | (1u << 3) | (1u << 16) | (1u << 17);
	buttons_ledR_off(PTB1_LEDR | PTB2_LEDR | PTB3_LEDR | PTB4_LEDR | PTB5_LEDR);

	/* nastav GPIO pro LEDxG */
	PORTD->PCR[3] = PORTD->PCR[4] = PORTD->PCR[5] = PORTD->PCR[6] = PORTD->PCR[7] = btnled_pcr;
	/* nastav na OUT rezim pro LEDxG */
	GPIOD->PDDR |= (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7);
	buttons_ledG_off(PTB1_LEDG | PTB2_LEDG | PTB3_LEDG | PTB4_LEDG | PTB5_LEDG);

	buttons_ledR_on(PTB1_LEDR | PTB2_LEDR | PTB3_LEDR | PTB4_LEDR | PTB5_LEDR);
	delay_ms(100);
	buttons_ledR_off(PTB1_LEDR | PTB2_LEDR | PTB3_LEDR | PTB4_LEDR | PTB5_LEDR);

	buttons_ledG_on(PTB1_LEDG | PTB2_LEDG | PTB3_LEDG | PTB4_LEDG | PTB5_LEDG);
	delay_ms(100);
	buttons_ledG_off(PTB1_LEDG | PTB2_LEDG | PTB3_LEDG | PTB4_LEDG | PTB5_LEDG);

	buttons_ledG_on(PTB1_LEDG | PTB3_LEDG | PTB5_LEDG);
	buttons_ledR_on(PTB2_LEDR | PTB3_LEDR | PTB4_LEDR);
	delay_ms(100);
	buttons_ledG_off(PTB1_LEDG | PTB3_LEDG | PTB5_LEDG);
	buttons_ledR_off(PTB2_LEDR | PTB3_LEDR | PTB4_LEDR);

	buttons_ledR_on(PTB1_LEDR | PTB3_LEDR | PTB5_LEDR);
	buttons_ledG_on(PTB2_LEDG | PTB3_LEDG | PTB4_LEDG);
	delay_ms(100);
	buttons_ledR_off(PTB1_LEDR | PTB3_LEDR | PTB5_LEDR);
	buttons_ledG_off(PTB2_LEDG | PTB3_LEDG | PTB4_LEDG);
}

static void led_rgb_init(){
	  /* pro ovladani LED pouzijeme GPIO vyvody (MUX1 -> PCR) */
	uint32_t btnled_pcr = PORT_PCR_MUX(1);

	/* nastav R, G, B vyvody na GPIO */
	PORTD->PCR[0] = PORTD->PCR[1] = PORTD->PCR[2] = btnled_pcr;
	/* nastav vyvody na OUT */
	GPIOD->PDDR |= 7;
	/* zhasni led zapisem 0 na vyvody */
	GPIOD->PDOR &= ~(7);

	/* test postupneho inidividualniho rozsvecovani R, G, B */
	/* R */
	GPIOD->PDOR |= 1;
	delay_ms(250);
	GPIOD->PDOR &= ~(1);
	/* G */
	GPIOD->PDOR |= 2;
	delay_ms(250);
	GPIOD->PDOR &= ~(2);
	/* B */
	GPIOD->PDOR |= 4;
	delay_ms(250);
	GPIOD->PDOR &= ~(4);
}

static void piezzo_init()
{
	PORTB->PCR[0]  = PORT_PCR_MUX(1);  /* PTB0  = GPIO (ALT1) */
	GPIOB->PDDR	|= (1u << 0);          /* OUT */
	GPIOB->PCOR	= (1u << 0);           /* -> log.0 */
}

static void beep(void)
{
    int i;
    for (i = 0; i < 10; i++) {
        GPIOB->PSOR = (1u << 0);    /* PTB0 high */
        delay_us(1500);
        GPIOB->PCOR = (1u << 0);    /* PTB0 low  */
        delay_us(1500);
    }
}

static void system_init(void)
{
    clock_init();
    SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK | SIM_SCGC5_PORTB_MASK
               |  SIM_SCGC5_PORTC_MASK | SIM_SCGC5_PORTE_MASK
			   |  SIM_SCGC5_PORTD_MASK;
    piezzo_init();
    beep();
    display_init();
    buttons_init();
    buttons_led_init();
    led_rgb_init();
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    uint32_t current_face[4];

    system_init();
    set_face(FACE_DEFAULT);
    welcome_animation();

    for (;;) {
        /* Zobrazeni, krok A: uloz volatile ISR promenne */
        __disable_irq();
        current_face[0] = face[0];
        current_face[1] = face[1];
        current_face[2] = face[2];
        current_face[3] = face[3];
        __enable_irq();

        /* Zobrazeni, krok B: zobraz hodnoty na cislicich (casovy multiplex) */
        show_on_dig123(DIG1, current_face[0]); delay_us(1000);
        show_on_dig123(DIG2, current_face[1]); delay_us(1000);
        show_on_dig123(DIG3, current_face[2]); delay_us(1000);
        show_on_dig4(current_face[3]);         delay_us(1000);

        /* Zobrazeni, krok C: obnoveni odpoctu po FACE_TIMEOUT cyclech */
        __disable_irq();   /* face_ticks sdileny s ISR -> atomicky RMW */
        if (face_ticks > 0) {
            face_ticks--;
            if (face_ticks == 0) {
                set_face(FACE_DEFAULT);
            }
        }
        __enable_irq();
    }
}
