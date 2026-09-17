#include "event_store.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
bool safe_name(const std::string &name)
{
    return !name.empty() &&
           name.size() <= 128 &&
           name.find_first_not_of(
               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
               "abcdefghijklmnopqrstuvwxyz"
               "0123456789_-") == std::string::npos;
}

bool read_file(
    const std::string &path,
    size_t maximum,
    std::string &data)
{
    data.clear();

    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(fd < 0)
        return false;

    struct stat info;
    bool ok = fstat(fd, &info) == 0 &&
              S_ISREG(info.st_mode) &&
              info.st_size > 0 &&
              static_cast<unsigned long long>(info.st_size) <= maximum;

    if(!ok)
    {
        close(fd);
        return false;
    }

    data.resize(static_cast<size_t>(info.st_size));
    size_t offset = 0;

    while(offset < data.size())
    {
        const ssize_t count = read(fd, &data[offset], data.size() - offset);

        if(count > 0)
        {
            offset += static_cast<size_t>(count);
        }
        else if(count < 0 && errno == EINTR)
        {
            continue;
        }
        else
        {
            ok = false;
            break;
        }
    }

    close(fd);

    if(!ok)
        data.clear();

    return ok;
}

bool sync_directory(const std::string &path)
{
    const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if(fd < 0)
        return false;

    const bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
}

std::string marker_path(
    const StoredEvent &event,
    const std::string &target)
{
    return event.directory + "/.uploaded_" + target;
}
} // namespace

bool event_store_list(
    const std::string &root,
    std::vector<std::string> &ids)
{
    ids.clear();

    DIR *directory = opendir(root.c_str());
    if(directory == NULL)
        return errno == ENOENT;

    errno = 0;
    struct dirent *entry;

    while((entry = readdir(directory)) != NULL)
    {
        const std::string name = entry->d_name;

        if(name.compare(0, 6, "event_") == 0 && safe_name(name))
            ids.push_back(name);

        errno = 0;
    }

    const bool ok = errno == 0;
    closedir(directory);

    std::sort(ids.begin(), ids.end());
    return ok;
}

bool event_store_load(
    const std::string &root,
    const std::string &id,
    StoredEvent &event)
{
    if(id.compare(0, 6, "event_") != 0 || !safe_name(id))
        return false;

    event = StoredEvent();
    event.id = id;
    event.directory = root + "/" + id;

    struct stat info;
    if(lstat(event.directory.c_str(), &info) != 0 ||
       !S_ISDIR(info.st_mode))
        return false;

    return read_file(event.directory + "/event.json",
                     64 * 1024, event.metadata) &&
           read_file(event.directory + "/image.jpg",
                     8 * 1024 * 1024, event.jpeg);
}

bool event_store_is_confirmed(
    const StoredEvent &event,
    const std::string &target)
{
    if(!safe_name(target))
        return false;

    std::string contents;
    return read_file(marker_path(event, target), 256, contents) &&
           contents == event.id + "\n";
}

bool event_store_confirm(
    const StoredEvent &event,
    const std::string &target)
{
    if(!safe_name(target))
        return false;

    std::string pattern = event.directory + "/.ack_XXXXXX";
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');

    const int fd = mkstemp(temporary.data());
    if(fd < 0)
        return false;

    const std::string contents = event.id + "\n";
    size_t offset = 0;
    bool ok = true;

    while(offset < contents.size())
    {
        const ssize_t count =
            write(fd, contents.data() + offset, contents.size() - offset);

        if(count > 0)
            offset += static_cast<size_t>(count);
        else if(count < 0 && errno == EINTR)
            continue;
        else
        {
            ok = false;
            break;
        }
    }

    if(ok && fsync(fd) != 0)
        ok = false;

    if(close(fd) != 0)
        ok = false;

    if(ok && rename(temporary.data(), marker_path(event, target).c_str()) != 0)
        ok = false;

    if(!ok)
    {
        unlink(temporary.data());
        return false;
    }

    return sync_directory(event.directory);
}

std::string event_store_base64(const std::string &bytes)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((bytes.size() + 2) / 3) * 4);

    for(size_t i = 0; i < bytes.size(); i += 3)
    {
        const unsigned int a = static_cast<unsigned char>(bytes[i]);
        const unsigned int b = i + 1 < bytes.size()
                                   ? static_cast<unsigned char>(bytes[i + 1])
                                   : 0;
        const unsigned int c = i + 2 < bytes.size()
                                   ? static_cast<unsigned char>(bytes[i + 2])
                                   : 0;

        const unsigned int value = (a << 16) | (b << 8) | c;

        output.push_back(alphabet[(value >> 18) & 63]);
        output.push_back(alphabet[(value >> 12) & 63]);
        output.push_back(i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=');
        output.push_back(i + 2 < bytes.size() ? alphabet[value & 63] : '=');
    }

    return output;
}