#ifndef USER_WB_SELECTION_H
#define USER_WB_SELECTION_H

/* Definition of GPIO pin parameters */
#define SELECTION_SIGNAL_IO_MUX  PERIPHS_IO_MUX_GPIO4_U
#define SELECTION_SIGNAL_IO_NUM  4
#define SELECTION_SIGNAL_IO_FUNC FUNC_GPIO4

void user_wb_selection_init(void);

#endif /* USER_WB_SELECTION_H */