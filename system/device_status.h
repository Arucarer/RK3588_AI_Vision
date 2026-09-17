#ifndef DEVICE_STATUS_H
#define DEVICE_STATUS_H

/*
 * 读取 Linux thermal zone 中最高的有效温度，单位摄氏度。
 * 不将它标记为 CPU 温度，因为 thermal zone 可能对应不同器件。
 *
 * 返回 true：读取成功。
 * 返回 false：没有可用数据，调用者应上报 null。
 */
bool device_read_max_temperature(double *temperature_c);

#endif