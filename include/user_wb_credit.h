#ifndef USER_WB_CREDIT_H
#define USER_WB_CREDIT_H

/* Definition of coin pulse durations */
#define COIN_NICKEL_PULSE_MS  50
#define COIN_DIME_PULSE_MS    40
#define COIN_QUARTER_PULSE_MS 60

/* Definition of GPIO pin parameters */
#define COIN_NICKEL_IO_MUX   PERIPHS_IO_MUX_GPIO5_U
#define COIN_NICKEL_IO_NUM   5
#define COIN_NICKEL_IO_FUNC  FUNC_GPIO5

#define COIN_DIME_IO_MUX     PERIPHS_IO_MUX_MTDI_U
#define COIN_DIME_IO_NUM     12
#define COIN_DIME_IO_FUNC    FUNC_GPIO12

#define COIN_QUARTER_IO_MUX  PERIPHS_IO_MUX_MTMS_U
#define COIN_QUARTER_IO_NUM  14
#define COIN_QUARTER_IO_FUNC FUNC_GPIO14

typedef enum CoinType {
    NICKEL = 0,
    DIME,
    QUARTER
} CoinType;

void user_wb_credit_init(void);

int user_wb_credit_drop(CoinType coin_type);

#endif /* USER_WB_CREDIT_H */