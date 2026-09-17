#include "device_status.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>

static bool is_thermal_zone(const char *name)
{
    const char prefix[] = "thermal_zone";
    const size_t prefix_length = sizeof(prefix) - 1;

    if(std::strncmp(name, prefix, prefix_length) != 0)
        return false;

    const char *suffix = name + prefix_length;

    if(*suffix == '\0')
        return false;

    for(; *suffix != '\0'; ++suffix)
    {
        if(*suffix < '0' || *suffix > '9')
            return false;
    }

    return true;
}

static bool read_temperature(const char *path, double *temperature_c)
{
    FILE *file = std::fopen(path, "r");

    if(file == NULL)
        return false;

    char text[128];

    const bool read_ok = std::fgets(text, sizeof(text), file) != NULL;
    const bool read_error = std::ferror(file) != 0;

    std::fclose(file);

    if(!read_ok || read_error)
        return false;

    errno = 0;
    char *end = NULL;

    const long value = std::strtol(text, &end, 10);

    if(errno != 0 || end == text)
        return false;

    while(*end == ' ' || *end == '\t' ||
          *end == '\r' || *end == '\n')
    {
        ++end;
    }

    if(*end != '\0')
        return false;

    /* thermal zone 的 temp 接口以毫摄氏度表示。 */
    const double temperature = value / 1000.0;

    if(!std::isfinite(temperature) ||
       temperature < -100.0 || temperature > 250.0)
    {
        return false;
    }

    *temperature_c = temperature;
    return true;
}

bool device_read_max_temperature(double *temperature_c)
{
    if(temperature_c == NULL)
        return false;

    *temperature_c = 0.0;

    DIR *directory = opendir("/sys/class/thermal");

    if(directory == NULL)
        return false;

    bool found = false;
    double maximum = 0.0;

    struct dirent *entry;

    while((entry = readdir(directory)) != NULL)
    {
        if(!is_thermal_zone(entry->d_name))
            continue;

        char path[512];

        const int length = std::snprintf(
            path,
            sizeof(path),
            "/sys/class/thermal/%s/temp",
            entry->d_name);

        if(length < 0 || static_cast<size_t>(length) >= sizeof(path))
            continue;

        double current = 0.0;

        if(!read_temperature(path, &current))
            continue;

        if(!found || current > maximum)
            maximum = current;

        found = true;
    }

    closedir(directory);

    if(found)
        *temperature_c = maximum;

    return found;
}