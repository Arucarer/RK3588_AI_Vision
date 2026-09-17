#include "event_store.h"
#include "common.h"

#include <cstdio>
#include <sys/statvfs.h>
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


namespace
{
int storage_max_count = 200;
int storage_min_free_mb = 128;

/* 为一次 JPEG 和 JSON 写入预留额外空间。不是磁盘配额保证。 */
const unsigned long long EVENT_WRITE_RESERVE = 10ULL * 1024 * 1024;

bool managed_name(const std::string &name)
{
    return name.compare(0, 6, "event_") == 0 ||
           name.compare(0, 9, ".pending_") == 0 ||
           name.compare(0, 4, ".gc_") == 0;
}

bool storage_status(
    const std::string &root,
    size_t &count,
    unsigned long long &free_bytes)
{
    count = 0;
    free_bytes = 0;

    struct statvfs space;
    if(statvfs(root.c_str(), &space) != 0)
        return false;

    free_bytes = static_cast<unsigned long long>(space.f_bavail) *
                 static_cast<unsigned long long>(space.f_frsize);

    DIR *directory = opendir(root.c_str());
    if(directory == NULL)
        return false;

    bool ok = true;

    for(;;)
    {
        errno = 0;
        struct dirent *entry = readdir(directory);

        if(entry == NULL)
        {
            if(errno != 0)
                ok = false;
            break;
        }

        if(managed_name(entry->d_name))
            count++;
    }

    closedir(directory);
    return ok;
}

/*
 * 清理目录只允许包含应用生成的普通文件。
 * 遇到未知文件、子目录或符号链接时保留，避免递归误删。
 */
bool remove_gc_directory(const std::string &path)
{
    struct stat info;
    if(lstat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
        return false;

    DIR *directory = opendir(path.c_str());
    if(directory == NULL)
        return false;

    std::vector<std::string> files;
    bool ok = true;

    for(;;)
    {
        errno = 0;
        struct dirent *entry = readdir(directory);

        if(entry == NULL)
        {
            if(errno != 0)
                ok = false;
            break;
        }

        const std::string name = entry->d_name;

        if(name == "." || name == "..")
            continue;

        const bool allowed =
            name == "image.jpg" ||
            name == "event.json" ||
            (name.compare(0, 10, ".uploaded_") == 0 &&
             safe_name(name.substr(10))) ||
            (name.compare(0, 5, ".ack_") == 0 &&
             safe_name(name.substr(5)));

        const std::string file = path + "/" + name;

        if(!allowed ||
           lstat(file.c_str(), &info) != 0 ||
           !S_ISREG(info.st_mode))
        {
            ok = false;
            break;
        }

        files.push_back(file);
    }

    closedir(directory);

    if(!ok)
        return false;

    for(const std::string &file : files)
    {
        if(unlink(file.c_str()) != 0 && errno != ENOENT)
            return false;
    }

    return rmdir(path.c_str()) == 0;
}

/*
 * .gc_ 目录是之前已确认、已进入清理阶段的事件。
 * 清理途中退出后，下次运行继续删除。
 */
void recover_gc(const std::string &root)
{
    DIR *directory = opendir(root.c_str());
    if(directory == NULL)
        return;

    std::vector<std::string> names;

    for(;;)
    {
        errno = 0;
        struct dirent *entry = readdir(directory);

        if(entry == NULL)
        {
            if(errno != 0)
                LOG_ERR("read GC directory failed");
            break;
        }

        const std::string name = entry->d_name;

        if(name.compare(0, 4, ".gc_") == 0 &&
           safe_name(name.substr(4)))
        {
            names.push_back(name);
        }
    }

    closedir(directory);

    bool changed = false;

    for(const std::string &name : names)
    {
        if(remove_gc_directory(root + "/" + name))
        {
            changed = true;
            LOG_INFO("event cleanup recovered: %s", name.c_str());
        }
        else
        {
            LOG_ERR("event cleanup incomplete: %s", name.c_str());
        }
    }

    if(changed && !sync_directory(root))
        LOG_ERR("sync event root after GC failed");
}
} // namespace

void event_store_set_limits(int max_count, int min_free_mb)
{
    storage_max_count = max_count;
    storage_min_free_mb = min_free_mb;
}

int event_store_check_capacity(const std::string &root)
{
    size_t count = 0;
    unsigned long long free_bytes = 0;

    if(!storage_status(root, count, free_bytes))
        return -1;

    const unsigned long long required =
        static_cast<unsigned long long>(storage_min_free_mb) *
        1024 * 1024 + EVENT_WRITE_RESERVE;

    if(count >= static_cast<size_t>(storage_max_count) ||
       free_bytes < required)
    {
        return 0;
    }

    return 1;
}

void event_store_cleanup(
    const std::string &root,
    const std::string &target)
{
    if(!safe_name(target))
        return;

    struct stat info;

    if(lstat(root.c_str(), &info) != 0)
    {
        if(errno != ENOENT)
            LOG_ERR("inspect event root failed");
        return;
    }

    if(!S_ISDIR(info.st_mode))
    {
        LOG_ERR("event root is not a directory");
        return;
    }

    recover_gc(root);

    int capacity = event_store_check_capacity(root);

    if(capacity > 0)
        return;

    if(capacity < 0)
    {
        LOG_ERR("event capacity check failed: %s", root.c_str());
        return;
    }

    std::vector<std::string> ids;

    if(!event_store_list(root, ids))
    {
        LOG_ERR("list events for cleanup failed");
        return;
    }

    /*
     * 当前事件 ID 包含时间戳，沿用目录名排序。
     * 系统时间回拨时，不保证严格的真实生成时间顺序。
     */
    for(const std::string &id : ids)
    {
        StoredEvent event;
        event.id = id;
        event.directory = root + "/" + id;

        if(lstat(event.directory.c_str(), &info) != 0 ||
           !S_ISDIR(info.st_mode))
        {
            continue;
        }

        /* 只清理当前目标服务端已确认的事件。 */
        if(!event_store_is_confirmed(event, target))
            continue;

        const std::string garbage = root + "/.gc_" + id;

        if(lstat(garbage.c_str(), &info) == 0)
            continue;

        if(errno != ENOENT)
        {
            LOG_ERR("inspect GC destination failed");
            continue;
        }

        /*
         * 先从可上传目录集合中移除，再删除文件。
         * TCP 上传与这里在同一个线程执行。
         */
        if(rename(event.directory.c_str(), garbage.c_str()) != 0)
        {
            LOG_ERR("move confirmed event to GC failed: %s", id.c_str());
            continue;
        }

        if(!sync_directory(root))
        {
            LOG_ERR("sync GC rename failed; retained: %s", id.c_str());
            return;
        }

        if(!remove_gc_directory(garbage))
        {
            LOG_ERR("event cleanup incomplete: %s", id.c_str());
            continue;
        }

        if(!sync_directory(root))
        {
            LOG_ERR("sync event cleanup failed");
            return;
        }

        LOG_INFO("confirmed event removed: %s", id.c_str());

        capacity = event_store_check_capacity(root);

        if(capacity > 0)
            return;

        if(capacity < 0)
        {
            LOG_ERR("event capacity recheck failed");
            return;
        }
    }

    /*
     * 无法回收时不删除未确认事件。
     * 抓拍线程会在提交文件前检查容量并跳过保存。
     */
}