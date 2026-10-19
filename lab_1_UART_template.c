/*
 * ============================================================
 * IMP  –  Lab 1  (SABLONA PRO STUDENTY)
 * komunikace pres UART, detekce udalosti dotazovanim (polling)
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
 * PTE16   UART2 TX   (ALT3)
 * PTE17   UART2 RX   (ALT3)
 * PTB0    piezzo bzucak (ALT1 - GPIO vystup)
 *
 * Nastaveni hodin
 * ---------------
 * 48 MHz interni IRC (HIRC)
 * 24 MHz kmitocet hodin sbernice (Bus clock)
 * UART2 SBR pro 24 MHz Bus clk a 115200 Bd: 
 *    SBR = 24 000 000 / (16 × 115 200) = 13  
 *    →  115 384 Bd  (0.16 % err)
 * ============================================================
 */

#include "MCXC143.h"

/* ── Applikacni data ─────────────────────────────────────────────────────── */

#define LOGIN_LEN 9 /* upravte, mate-li FIT login o jinem poctu znaku */

static char correct_login[LOGIN_LEN + 1] = "xondruj00";   /* nahradte xlogin00 svym FIT loginem */
static char entered_login[LOGIN_LEN + 1];
static char prompt[7] = "Login>";                       

/* ── pomocne funkce ──────────────────────────────────────────────────────── */

/* Cekani pres SysTick pri 48 MHz core clock a 24-bit citaci je max ~349 ms. */
#define SYSTICK_MAX_US 349000UL
/* Cekej aktivne 1 us. */
static void delay_us(uint32_t us) {
    if (!us) return;
    if (us > SYSTICK_MAX_US) us = SYSTICK_MAX_US;
    SysTick->CTRL = 0; SysTick->VAL = 0;
    SysTick->LOAD = 48UL * us - 1UL;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)) {}
    SysTick->CTRL = 0;
}
/* Cekej aktivne 1 ms. */
static void delay_ms(uint32_t ms) {
    uint32_t c = ms / 349; while (c--) delay_us(349000UL);
    uint32_t r = ms % 349; if (r) delay_us(r * 1000UL);
}

/* Porovnej prvnich n znaku v retezcich A a B; 
   vrat 0 pri prvni neshode, jinak vrat 1 (pri celkove shode). */
static int str_equal(const char *strA, const char *strB, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (strA[i] != strB[i]) return 0;
    }
    return 1;
}

/* ── UART2 I/O ───────────────────────────────────────────────────────────── */

/* Cekej dokud vysilac neskonci vysilani, az pak odesli novy znak (ch). */
static void uart_send_char(char ch)
{
    while (!(UART2->S1 & UART_S1_TDRE_MASK)) { }
    UART2->D = (uint8_t)ch;
}

static char uart_recv_char(void)
{
    /* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! *
     * UKOL 1: Doprogramujte funkci pro prijeti jednoho znaku pres UART.          *
     * a) Ceka dokud priznak Receive Data Register Full (RDRF) v registru S1      *
     * periferie UART2 neni nastaven na hodnotu 1.                                *
     * b) Precte a vrati hodnotu prijateho znaku z registru (D) periferie UART2.  *
     * Pozn.: Precteni obsahu D nuluje priznak RDRF.                              *
     * Tip: Pri implementaci se muzete inspirovat telem uart_send_char()          *
     * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */
     
    // DOPLNTE: a) Cekani na prichod znaku
    while (!(UART2->S1 & UART_S1_RDRF_MASK)) { }

    // DOPLNTE: b) Precteni hodnoty prichoziho znaku a jeji vraceni (coby navratovou hodnotu funkce)
    return (char)UART2->D;
}

/* Odesli pres UART kazdy znak retezce zakonceneho nulovou hodnotou (null-terminated). */
static void uart_send_str(const char *str)
{
    while (*str) { uart_send_char(*str++); }
}

/* Posun kurzor v terminalu na zacatek noveho radku. */
static void uart_newline(void)
{
    uart_send_char('\r');
    uart_send_char('\n');
}

/* ── Bzucak ──────────────────────────────────────────────────────────────── */

/* 500 cyklu obdelnikoveho signalu na vstupu bzucaku produkuje ~1 kHz ton.    */
static void beep(void)
{
    int i;
    for (i = 0; i < 500; i++) {
        GPIOB->PSOR = (1u << 0);    /* PTB0 do log.1/High */
        delay_us(500);
        GPIOB->PCOR = (1u << 0);    /* PTB0 do log.0/Low  */
        delay_us(500);
    }
}

/* ── Inicializace ────────────────────────────────────────────────────────── */

/*
 * Nastav hodiny MCU na 48 MHz IRC.
 *
 * Po resetu FLL bezi na ~21 MHz.  Kroky pro dosazeni 48 MHz IRC:
 * 1. Povol 48 MHz high-speed IRC (HIRC) pres MCG->MC.
 * 2. Vyber HIRC zapisem CLKS = 2 do MCG->C1.
 * 3. Cekej na MCG->S[CLKST] = 1 pro potvrzeni dokonceni prechodu.
 * 4. Nastav OUTDIV1 = /1 (core 48 MHz), OUTDIV4 = /2 (bus 24 MHz).
 */
static void clock_init(void)
{
    SIM->COPC = SIM_COPC_COPT(0);          

    MCG->MC |= MCG_MC_HIRCEN_MASK;         

    MCG->C1 = (uint8_t)(MCG->C1 & ~MCG_C1_CLKS_MASK) | MCG_C1_CLKS(0);
    while ((MCG->S & MCG_S_CLKST_MASK) != MCG_S_CLKST(0)) { }

    SIM->CLKDIV1 = SIM_CLKDIV1_OUTDIV1(0) | SIM_CLKDIV1_OUTDIV4(1);
}

/* Nastav PTE16/17 pro UART2 RX/Tx, PTB0 pro buzzer GPIO. */
static void pins_init(void)
{
    SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK | SIM_SCGC5_PORTE_MASK;

    PORTE->PCR[16] = PORT_PCR_MUX(3);      /* PTE16 = UART2_TX  (ALT3) */
    PORTE->PCR[17] = PORT_PCR_MUX(3);      /* PTE17 = UART2_RX  (ALT3) */

    PORTB->PCR[0]  = PORT_PCR_MUX(1);      /* PTB0  = GPIO      (ALT1) */
    GPIOB->PDDR   |= (1u << 0);            
    GPIOB->PCOR    = (1u << 0);            
}

/* UART2: 115 200 Bd, 8 data bits, no parity, 1 stop bit. */
static void uart2_init(void)
{
    SIM->SCGC4 |= SIM_SCGC4_UART2_MASK;   

    UART2->C2 &= (uint8_t)~(UART_C2_TE_MASK | UART_C2_RE_MASK);

    UART2->BDH = 0x00;
    UART2->BDL = 0x0D;                     /* SBR = 13                  */
    UART2->C1  = 0x00;                     /* 8 data bits, no parity    */
    UART2->C3  = 0x00;                     

    (void)UART2->S1;
    UART2->D = 0x00;

    UART2->C2 |= (uint8_t)(UART_C2_TE_MASK | UART_C2_RE_MASK);
}

/* ── HLAVNI PROGRAM ──────────────────────────────────────────────────────── */

int main(void)
{
    int i;

    clock_init();
    pins_init();
    uart2_init();

    /* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! *
     * UKOL 2: Doplnte chybejici funkcionalitu v main().                          *
     *                                                                            *
     * Program bude opakovat nasledujici sekvenci, dokud uzivatel nezada pres     * 
     * PC terminal spravny retezec (ulozeny v poli correct_login[])               *
     *   1. Zobrazi prompt "Login>" v PC terminalu.                               *
     *   2. Z PC terminalu nacte, znak po znaku, LOGIN_LEN znaku, pricemz kazdy   *
     *      z nactenych znaku ulozi do pole entered_login[] a posle zpet (echo)   *
     *      na PC terminal.                                                       *
     *   3. Porovna obsahy entered_login[] a correct_login[] pomoci str_equal().  *
     *      - V pripade neshody vypise do PC terminalu zpravu "ERR", prejde na    *
     *        novy radek (uart_newline()), a zacne od kroku 1.                    *
     *      - V pripade shody opusti smycku, a vyda zvukovy signal pomoci beep(). * 
     * Program skonci ve smycce while(1){}, branici navratu z main().             *
     * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */
    do {
        uart_send_str(prompt);

        // DOPLNTE: Smycka pro ukladani prijatych znaku do entered_login[]
        for (i = 0; i < LOGIN_LEN; i++) {
            entered_login[i] = uart_recv_char();
            uart_send_char(entered_login[i]);
        }
        entered_login[LOGIN_LEN] = '\0';

        uart_newline();

        // DOPLNTE: Porovnani obsahu poli entered_login[] a correct_login[] a reakce na jejich ne/shodu
        if (!str_equal(entered_login, correct_login, LOGIN_LEN)) {
            uart_send_str("ERR");
            uart_newline();
        }

    } while (!str_equal(entered_login, correct_login, LOGIN_LEN));

    beep();

    while (1) { }    
}
