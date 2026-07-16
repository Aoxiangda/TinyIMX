#pragma once

#include "common/logging/LogFormatter.h"
#include "common/logging/LogSink.h"

#include <fstream>
#include <cstdint>
#include <memory>
#include <string>

namespace tinyimx {
class FileSink : public LogSink {
public:
    FileSink(std::string file_path,
             std::uintmax_t max_file_size_bytes,
             int max_backup_files,
             bool flush_each_log);
    ~FileSink() override;


    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;

    bool Open();
    bool Log(const LogMessage& message) override;
    void Flush() override;


    const std::string& FilePath() const;
    bool IsOpen() const;


private:
    bool RotateIfNeeded(std::uintmax_t append_size);
    bool RotateFiles();
    bool ReopenAfterRotate();

private:
    std::string file_path_;
    std::uintmax_t max_file_size_bytes_{0};

    int max_backup_files_{0};
    bool flush_each_log_{false};

    std::uintmax_t current_file_size_{0};
    std::ofstream file_;
    std::unique_ptr<LogFormatter> formatter_;

}; //
}