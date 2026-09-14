/* test_runtime.c - CPU semantics, the RAM/SFR map, and the LCD layout.
 *
 * All inputs are synthetic. The flag rules checked here are the ones the
 * documentation states: CY is carry out of bit 7, AC out of bit 3, OV signed
 * overflow, and INC/DEC/AND/OR/XOR touch no flags at all.
 */
#include "vmurecomp/recomp_rt.h"
#include "vmurecomp/lcd.h"
#include "vmurecomp/timer.h"
/* Tests must assert even in a release build, where CMake defines NDEBUG. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t rom[65536];

static void boot(void) {
    memset(rom, 0, sizeof(rom));
    vmu_reset(rom, sizeof(rom));
}

#define ACC_IS(v)  assert(vmu_acc() == (v))
#define CY_IS(v)   assert(((vmu_psw() & PSW_CY) != 0) == (v))
#define AC_IS(v)   assert(((vmu_psw() & PSW_AC) != 0) == (v))
#define OV_IS(v)   assert(((vmu_psw() & PSW_OV) != 0) == (v))

static void test_add_flags(void) {
    boot();

    vmu_set_acc(0x0F); vmu_rt_add(0x01);
    ACC_IS(0x10); CY_IS(0); AC_IS(1); OV_IS(0);

    vmu_set_acc(0xFF); vmu_rt_add(0x01);
    ACC_IS(0x00); CY_IS(1); AC_IS(1);

    /* 0x7F + 0x01 crosses the sign boundary: signed overflow, no carry. */
    vmu_set_acc(0x7F); vmu_rt_add(0x01);
    ACC_IS(0x80); CY_IS(0); OV_IS(1);

    /* 0x80 + 0x80 wraps to zero: carry out, and also signed overflow. */
    vmu_set_acc(0x80); vmu_rt_add(0x80);
    ACC_IS(0x00); CY_IS(1); OV_IS(1);

    /* ADDC folds the incoming carry in. */
    vmu_set_acc(0x10); vmu_rt_add(0xF0);        /* leaves CY set */
    CY_IS(1);
    vmu_set_acc(0x01); vmu_rt_addc(0x01);
    ACC_IS(0x03);
}

static void test_sub_flags(void) {
    boot();

    vmu_set_acc(0x10); vmu_rt_sub(0x01);
    ACC_IS(0x0F); CY_IS(0); AC_IS(1);           /* borrow into bit 3 */

    vmu_set_acc(0x00); vmu_rt_sub(0x01);
    ACC_IS(0xFF); CY_IS(1);                     /* borrow out */

    /* 0x80 - 0x01 leaves the negative range: signed overflow. */
    vmu_set_acc(0x80); vmu_rt_sub(0x01);
    ACC_IS(0x7F); OV_IS(1);

    vmu_set_acc(0x05); vmu_rt_sub(0x05);
    ACC_IS(0x00); CY_IS(0);

    /* SUBC subtracts the borrow as well. */
    vmu_set_acc(0x00); vmu_rt_sub(0x01);        /* sets CY */
    vmu_set_acc(0x10); vmu_rt_subc(0x01);
    ACC_IS(0x0E);
}

static void test_mul_div(void) {
    boot();

    /* (ACC:C) * B -> B:ACC:C */
    vmu_set_acc(0x01); vmu_write(SFR_C, 0x00); vmu_write(SFR_B, 0x02);
    vmu_rt_mul();                                /* 0x0100 * 2 = 0x000200 */
    assert(vmu_read(SFR_B) == 0x00);
    ACC_IS(0x02);
    assert(vmu_read(SFR_C) == 0x00);
    CY_IS(0); OV_IS(0);

    /* a product past 16 bits sets OV */
    vmu_set_acc(0xFF); vmu_write(SFR_C, 0xFF); vmu_write(SFR_B, 0x02);
    vmu_rt_mul();
    OV_IS(1);
    assert(vmu_read(SFR_B) == 0x01);

    /* (ACC:C) / B -> quotient in ACC:C, remainder in B */
    vmu_set_acc(0x01); vmu_write(SFR_C, 0x00); vmu_write(SFR_B, 0x10);
    vmu_rt_div();                                /* 0x0100 / 0x10 = 0x10 r0 */
    ACC_IS(0x00);
    assert(vmu_read(SFR_C) == 0x10);
    assert(vmu_read(SFR_B) == 0x00);
    OV_IS(1);                                    /* OV marks a zero remainder */

    vmu_set_acc(0x00); vmu_write(SFR_C, 0x07); vmu_write(SFR_B, 0x02);
    vmu_rt_div();                                /* 7 / 2 = 3 r1 */
    assert(vmu_read(SFR_C) == 0x03);
    assert(vmu_read(SFR_B) == 0x01);
    OV_IS(0);

    /* divide by zero sets OV rather than trapping */
    vmu_write(SFR_B, 0x00);
    vmu_rt_div();
    OV_IS(1);
}

static void test_rotates(void) {
    boot();

    vmu_set_acc(0x81); vmu_rt_rol();  ACC_IS(0x03);
    vmu_set_acc(0x81); vmu_rt_ror();  ACC_IS(0xC0);

    /* ROLC pushes bit 7 into CY and pulls the old CY into bit 0. */
    vmu_set_acc(0x80);
    vmu_write(SFR_PSW, 0);
    vmu_rt_rolc();
    ACC_IS(0x00); CY_IS(1);
    vmu_rt_rolc();
    ACC_IS(0x01); CY_IS(0);

    vmu_set_acc(0x01);
    vmu_write(SFR_PSW, 0);
    vmu_rt_rorc();
    ACC_IS(0x00); CY_IS(1);
    vmu_rt_rorc();
    ACC_IS(0x80); CY_IS(0);
}

static void test_logic_leaves_flags(void) {
    boot();
    vmu_set_acc(0xFF); vmu_rt_add(0x01);        /* set CY and AC */
    uint8_t before = vmu_psw();
    vmu_set_acc(0xF0); vmu_rt_and(0x0F);
    ACC_IS(0x00);
    /* P tracks ACC and is recomputed on read, so compare everything else. */
    assert((vmu_psw() & ~PSW_P) == (before & ~PSW_P));

    vmu_rt_or(0x0F);  ACC_IS(0x0F);
    vmu_rt_xor(0xFF); ACC_IS(0xF0);
    assert((vmu_psw() & ~PSW_P) == (before & ~PSW_P));
}

static void test_bit_ops(void) {
    boot();
    vmu_write(0x030, 0x00);
    vmu_rt_set1(0x030, 3);
    assert(vmu_read(0x030) == 0x08);
    assert(vmu_rt_bp(0x030, 3) == 1);
    assert(vmu_rt_bp(0x030, 2) == 0);

    vmu_rt_not1(0x030, 3);
    assert(vmu_read(0x030) == 0x00);

    vmu_rt_set1(0x030, 7);
    /* BPC reports the bit and clears it in the same step. */
    assert(vmu_rt_bpc(0x030, 7) == 1);
    assert(vmu_read(0x030) == 0x00);
    assert(vmu_rt_bpc(0x030, 7) == 0);

    vmu_rt_set1(0x030, 1);
    vmu_rt_clr1(0x030, 1);
    assert(vmu_read(0x030) == 0x00);
}

static void test_compare_sets_carry(void) {
    boot();
    /* BE/BNE report equality and leave CY = (left < right). */
    vmu_set_acc(0x05);
    assert(vmu_rt_cmp_acc(0x05) == 1); CY_IS(0);
    assert(vmu_rt_cmp_acc(0x06) == 0); CY_IS(1);
    assert(vmu_rt_cmp_acc(0x04) == 0); CY_IS(0);

    vmu_write(0x040, 0x10);
    assert(vmu_rt_cmp_at(0x040, 0x20) == 0); CY_IS(1);
    assert(vmu_rt_cmp_at(0x040, 0x10) == 1); CY_IS(0);
}

static void test_dbnz(void) {
    boot();
    vmu_write(0x050, 0x02);
    assert(vmu_rt_dbnz(0x050) == 1);
    assert(vmu_read(0x050) == 0x01);
    assert(vmu_rt_dbnz(0x050) == 0);
    assert(vmu_read(0x050) == 0x00);
    /* and it wraps rather than sticking at zero */
    assert(vmu_rt_dbnz(0x050) == 1);
    assert(vmu_read(0x050) == 0xFF);
}

static void test_ram_banking(void) {
    boot();
    /* Bank 0 is the firmware's, bank 1 the application's; the same address
     * must reach different storage. */
    vmu_write(SFR_PSW, 0);
    vmu_write(0x020, 0xAA);
    vmu_write(SFR_PSW, PSW_RAMBK0);
    vmu_write(0x020, 0x55);
    assert(vmu_read(0x020) == 0x55);
    vmu_write(SFR_PSW, 0);
    assert(vmu_read(0x020) == 0xAA);
}

static void test_indirect(void) {
    boot();
    vmu_write(SFR_PSW, 0);                  /* IRBK = 0, RAM bank 0 */
    vmu_write(0x000, 0x40);                 /* @R0 points at 0x040 */
    vmu_write(0x002, 0x44);                 /* @R2 points at 0x144 */
    assert(vmu_ind(0) == 0x040);
    assert(vmu_ind(2) == 0x144);            /* bit 1 of i selects the SFR half */

    /* IRBK shifts which cells hold the pointers. */
    vmu_write(SFR_PSW, PSW_IRBK0);          /* IRBK = 1 -> cells 004..007 */
    vmu_write(0x004, 0x60);
    assert(vmu_ind(0) == 0x060);
}

static void test_stack(void) {
    boot();
    /* SP starts at 0x7F and the first push lands at 0x80. */
    assert(vmu_read(SFR_SP) == 0x7F);
    vmu_push_byte(0x11);
    vmu_push_byte(0x22);
    assert(vmu_read(SFR_SP) == 0x81);
    assert(vmu_pop_byte() == 0x22);
    assert(vmu_pop_byte() == 0x11);
    assert(vmu_read(SFR_SP) == 0x7F);

    /* The stack stays in bank 0 even with the application bank selected. */
    vmu_write(SFR_PSW, PSW_RAMBK0);
    vmu_push_byte(0x33);
    vmu_write(SFR_PSW, 0);
    assert(vmu_read(0x080) == 0x33);
}

static void test_parity_is_read_only(void) {
    boot();
    vmu_set_acc(0x07);                      /* three bits set: odd */
    assert(vmu_read(SFR_PSW) & PSW_P);
    vmu_set_acc(0x03);                      /* two bits set: even */
    assert(!(vmu_read(SFR_PSW) & PSW_P));
    /* writing P has no effect */
    vmu_write(SFR_PSW, PSW_P);
    assert(!(vmu_read(SFR_PSW) & PSW_P));
}

static void test_buttons_are_active_low(void) {
    boot();
    assert(vmu_read(SFR_P3) == 0xFF);
    vmu.buttons = BTN_A;
    assert(vmu_read(SFR_P3) == (uint8_t)~BTN_A);
}

static void test_lcd_layout(void) {
    boot();
    /* Line pairs are 12 bytes with a 4-byte skip, so line 2 starts at +16. */
    vmu.xram[0][0] = 0x80;                  /* line 0, leftmost pixel  */
    vmu.xram[0][5] = 0x01;                  /* line 0, rightmost pixel */
    vmu.xram[0][6] = 0x80;                  /* line 1, leftmost        */
    vmu.xram[0][16] = 0x80;                 /* line 2, leftmost        */
    vmu.xram[1][0] = 0x80;                  /* line 16, leftmost       */

    assert(vmu_lcd_pixel(0, 0) == 1);
    assert(vmu_lcd_pixel(1, 0) == 0);
    assert(vmu_lcd_pixel(47, 0) == 1);
    assert(vmu_lcd_pixel(0, 1) == 1);
    assert(vmu_lcd_pixel(0, 2) == 1);
    assert(vmu_lcd_pixel(0, 3) == 0);
    assert(vmu_lcd_pixel(0, 16) == 1);

    /* out of range reads are zero, not a crash */
    assert(vmu_lcd_pixel(-1, 0) == 0);
    assert(vmu_lcd_pixel(48, 0) == 0);
    assert(vmu_lcd_pixel(0, 32) == 0);
}

static void test_xram_is_banked_through_the_window(void) {
    boot();
    vmu_write(SFR_XBNK, 0);
    vmu_write(XRAM_BASE, 0xF0);
    vmu_write(SFR_XBNK, 1);
    vmu_write(XRAM_BASE, 0x0F);

    vmu_write(SFR_XBNK, 0);
    assert(vmu_read(XRAM_BASE) == 0xF0);
    assert(vmu_lcd_pixel(0, 0) == 1);       /* bank 0 is the top half    */
    assert(vmu_lcd_pixel(0, 16) == 0);      /* bank 1 holds 0x0F         */
    assert(vmu_lcd_pixel(7, 16) == 1);
}

static void test_dispatch_registry(void) {
    boot();
    vmu_rt_clear_registry();
    assert(vmu_rt_lookup(0x1234) == NULL);

    /* An unmapped tail call traps instead of jumping into nothing. */
    unsigned before = vmu_rt_trap_count();
    vmu_rt_dispatch(0x1234);
    assert(vmu_rt_trap_count() == before + 1);
    assert(vmu_rt_last_trap() == 0x1234);
    vmu_rt_clear_registry();
}

static void test_call_ret_balances_sp(void) {
    boot();
    vmu_rt_clear_registry();
    uint8_t sp0 = vmu_read(SFR_SP);
    /* No function registered, so the call traps - but the push/pop pair must
     * still leave SP where it started once RET runs. */
    vmu_rt_call(0x2000, 0x0105);
    assert(vmu_read(SFR_SP) == (uint8_t)(sp0 + 2));
    vmu_rt_ret();
    assert(vmu_read(SFR_SP) == sp0);
    vmu_rt_clear_registry();
}

static void test_work_ram_autoincrements(void) {
    boot();
    vmu_write(SFR_VSEL, 0x10);              /* enable auto-increment */
    vmu_write(SFR_VRMAD1, 0x00);
    vmu_write(SFR_VRMAD2, 0x00);
    vmu_write(SFR_VTRBF, 0xAB);
    vmu_write(SFR_VTRBF, 0xCD);
    assert(vmu_read(SFR_VRMAD1) == 0x02);

    vmu_write(SFR_VRMAD1, 0x00);
    assert(vmu_read(SFR_VTRBF) == 0xAB);
    assert(vmu_read(SFR_VTRBF) == 0xCD);
}

int main(void) {
    test_add_flags();
    test_sub_flags();
    test_mul_div();
    test_rotates();
    test_logic_leaves_flags();
    test_bit_ops();
    test_compare_sets_carry();
    test_dbnz();
    test_ram_banking();
    test_indirect();
    test_stack();
    test_parity_is_read_only();
    test_buttons_are_active_low();
    test_lcd_layout();
    test_xram_is_banked_through_the_window();
    test_dispatch_registry();
    test_call_ret_balances_sp();
    test_work_ram_autoincrements();
    printf("test_runtime: ok\n");
    return 0;
}
