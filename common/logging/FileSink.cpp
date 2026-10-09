#include "common/logging/FileSink.h"

#include <filesystem>
#include <system_error>
#include <utility>


namespace tinyimx {

namespace {
    std:: filesystem::path BackupPath(const std::filesystem::path& path, int index) {
        return std::filesystem::path(path.string() + "." + std::to_string(index));
    }
} // namespace

FileSink::FileSink(std::string file_path,
                   std::uintmax_t max_file_size_bytes,
                   int max_backup_files,
                   bool flush_each_log)
    : file_path_(std::move(file_path)),
      max_file_size_bytes_(max_file_size_bytes),
      max_backup_files_(max_backup_files),
      flush_each_log_(flush_each_log),
      formatter_(std::make_unique<DefaultLogFormatter>()) {}

FileSink::~FileSink() {
    Flush();

    if (file_.is_open()) {
        file_.close();
    }
}

bool FileSink::Open() {
    try {
        std::filesystem::path path(file_path_);
        const auto parent_path = path.parent_path();

        if (!parent_path.empty()) {
            std::filesystem::create_directories(parent_path);
        }

        if (std::filesystem::exists(path)) {
            current_file_size_ = std::filesystem::file_size(path);
        } else {
            current_file_size_ = 0;
        }

        file_.open(file_path_, std::ios::app);
        return file_.is_open();
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool FileSink::Log(const LogMessage& message) {
    if (!file_.is_open()) {
        return false;
    }

    const std::string formatted_message = formatter_->Format(message);
    const std::uintmax_t append_size =
        static_cast<std::uintmax_t>(formatted_message.size() + 1);

    if (!RotateIfNeeded(append_size)) {
        return false;
    }

    file_ << formatted_message << '\n';

    if (!file_) {
        return false;
    }

    current_file_size_ += append_size;

    if (flush_each_log_) {
        file_.flush();
    }

    return true;
}

void FileSink::Flush() {
    if (file_.is_open()) {
        file_.flush();
    }
}

const std::string& FileSink::FilePath() const {
    return file_path_;
}

bool FileSink::IsOpen() const {
    return file_.is_open();
}

bool FileSink::RotateIfNeeded(std::uintmax_t append_size) {
    if (max_file_size_bytes_ == 0) {
        return true;
    }

    if (current_file_size_ + append_size <= max_file_size_bytes_) {
        return true;
    }

    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }

    if (!RotateFiles()) {
        return false;
    }

    return ReopenAfterRotate();
}

bool FileSink::RotateFiles() {
    try {
        const std::filesystem::path path(file_path_);

        if (max_backup_files_ <= 0) {
            std::error_code remove_error;
            std::filesystem::remove(path, remove_error);
            return true;
        }

        const std::filesystem::path oldest_backup =
            BackupPath(path, max_backup_files_);

        std::error_code remove_error;
        std::filesystem::remove(oldest_backup, remove_error);

        for (int i = max_backup_files_ - 1; i >= 1; --i) {
            const std::filesystem::path from = BackupPath(path, i);
            const std::filesystem::path to = BackupPath(path, i + 1);

            if (!std::filesystem::exists(from)) {
                continue;
            }

            std::error_code rename_error;
            std::filesystem::rename(from, to, rename_error);

            if (rename_error) {
                return false;
            }
        }

        if (std::filesystem::exists(path)) {
            const std::filesystem::path first_backup = BackupPath(path, 1);

            std::error_code rename_error;
            std::filesystem::rename(path, first_backup, rename_error);

            if (rename_error) {
                return false;
            }
        }

        return true;
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool FileSink::ReopenAfterRotate() {
    current_file_size_ = 0;

    file_.open(file_path_, std::ios::app);
    return file_.is_open();
}


} // namespace tinyimx