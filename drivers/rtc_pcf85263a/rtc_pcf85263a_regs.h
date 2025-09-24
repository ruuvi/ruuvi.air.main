/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef ZEPHYR_DRIVERS_RTC_PCF85263A_REGS_H_
#define ZEPHYR_DRIVERS_RTC_PCF85263A_REGS_H_

#include <stdint.h>

/* PCF85263A device registers */
#define PCF85263A_REG_100TH_SECONDS (0x00)
#define PCF85263A_REG_SECONDS       (0x01)
#define PCF85263A_REG_MINUTES       (0x02)
#define PCF85263A_REG_HOURS         (0x03)
#define PCF85263A_REG_DAYS          (0x04)
#define PCF85263A_REG_WEEKDAYS      (0x05)
#define PCF85263A_REG_MONTH         (0x06)
#define PCF85263A_REG_YEARS         (0x07)
#define PCF85263A_REG_PIN_IO        (0x27)
#define PCF85263A_REG_FUNCTION      (0x28)
#define PCF85263A_REG_INTA_ENABLE   (0x29)
#define PCF85263A_REG_FLAGS         (0x2B)
#define PCF85263A_REG_STOP_ENABLE   (0x2E)
#define PCF85263A_REG_RESET         (0x2F)
#define PCF85263A_WRAP_AROUND_REG   (PCF85263A_REG_RESET + 1)

/* Register masks to clear status bits */
#define PCF85263A_REG_SECONDS_MASK          GENMASK(6, 0)
#define PCF85263A_REG_SECONDS_OSC_STOP_MASK BIT(7)
#define PCF85263A_REG_MINUTES_MASK          GENMASK(6, 0)
#define PCF85263A_REG_HOURS_MASK            GENMASK(5, 0)
#define PCF85263A_REG_DAYS_MASK             GENMASK(5, 0)
#define PCF85263A_REG_WEEKDAYS_MASK         GENMASK(2, 0)
#define PCF85263A_REG_MONTHS_MASK           GENMASK(4, 0)

#define PCF85263A_CALC_NUM_REGS(first_reg, last_reg)                                               \
	(((last_reg) >= (first_reg)) ? ((last_reg) - (first_reg) + 1)                              \
				     : ((last_reg) + PCF85263A_WRAP_AROUND_REG - (first_reg) + 1))

#define PCF85263A_REG_PIN_IO_INTAPM_MASK    GENMASK(1, 0) /*  */
#define PCF85263A_REG_PIN_IO_INTAPM_CLK     (0 << 0)      /*  */
#define PCF85263A_REG_PIN_IO_INTAPM_BATTERY (1 << 0)      /*  */
#define PCF85263A_REG_PIN_IO_INTAPM_INTA    (2 << 0)      /* INTA pin mode to INTA output */
#define PCF85263A_REG_PIN_IO_INTAPM_HI_Z    (3 << 0)      /* */

#define PCF85263A_REG_FUNC_COF_MASK           GENMASK(2, 0) /* Clock output frequency bitmask */
#define PCF85263A_REG_FUNC_COF_1_HZ           (6 << 0)      /* Clock output frequency 1 Hz */
#define PCF85263A_REG_FUNC_PI_MASK            GENMASK(6, 5) /* Periodic interrupt bitmask */
#define PCF85263A_REG_FUNC_PI_NONE            (0 << 5)      /* Periodic interrupt: None */
#define PCF85263A_REG_FUNC_PI_ONCE_PER_SECOND (1 << 5) /* Periodic interrupt: Once per second */

#define PCF85263A_REG_INTA_ENABLE_PIEA BIT(6) /* Enable periodic interrupt on INTA */

#define PCF85263A_REG_FLAGS_PIF   BIT(7) /* Periodic interrupt flag */
#define PCF85263A_REG_FLAGS_A2F   BIT(6) /* Alarm 2 flag */
#define PCF85263A_REG_FLAGS_A1F   BIT(5) /* Alarm 1 flag */
#define PCF85263A_REG_FLAGS_WDF   BIT(4) /* WatchDog flag */
#define PCF85263A_REG_FLAGS_BSF   BIT(3) /* Battery Switch flag */
#define PCF85263A_REG_FLAGS_TSR3F BIT(2) /* Timestamp Register 3 event flag */
#define PCF85263A_REG_FLAGS_TSR2F BIT(1) /* Timestamp Register 2 event flag */
#define PCF85263A_REG_FLAGS_TSR1F BIT(0) /* Timestamp Register 1 event flag */

#define PCF85263A_REG_STOP_ENABLE_STOP_MASK      0x01 /* */
#define PCF85263A_REG_STOP_ENABLE_STOP_BIT_SET   0x01 /* Stop the clock */
#define PCF85263A_REG_STOP_ENABLE_STOP_BIT_CLEAR 0x00 /* Start the clock */

#define PCF85263A_REG_RESET_CMD_CPR     0xA4 /* Clear prescaler */
#define PCF85263A_REG_RESET_CMD_CTS     0x25 /* Clear timestamp */
#define PCF85263A_REG_RESET_CMD_CPR_CTS 0xA5 /* Clear prescaler and timestamp */
#define PCF85263A_REG_RESET_CMD_SR      0x2C /* Software reset */

/* Supported time fields for this RTC */
#define PCF85263A_RTC_TIME_MASK                                                                    \
	(RTC_ALARM_TIME_MASK_SECOND | RTC_ALARM_TIME_MASK_MINUTE | RTC_ALARM_TIME_MASK_HOUR |      \
	 RTC_ALARM_TIME_MASK_MONTH | RTC_ALARM_TIME_MASK_MONTHDAY | RTC_ALARM_TIME_MASK_YEAR |     \
	 RTC_ALARM_TIME_MASK_WEEKDAY)

#endif /* ZEPHYR_DRIVERS_RTC_PCF85263A_REGS_H_ */
