#include "infrastructure/file_log_writer.h"

#include "domain/log_file_naming.h"

#include <chrono>
#include <cstddef>
#include <utility>

namespace gitman {
    namespace {
        constexpr std::u8string_view line_break { u8"\r\n" };
        // 파일 이름이 이미 있을 때 뒤에 번호를 붙여 다시 시도하는 횟수다.
        constexpr std::size_t file_name_attempts { 100 };

        // 유실 안내 문구다. 파일에만 남는다.
        std::u8string dropped_text(const std::size_t count)
        {
            std::u8string digits {};
            std::size_t remaining { count };
            do
            {
                digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + remaining % 10));
                remaining /= 10;
            } while (remaining > 0);
            return digits + u8"줄이 유실되었습니다 (기록이 밀렸습니다)";
        }

        std::u8string join_path(const std::u8string_view left, const std::u8string_view right)
        {
            std::u8string result { left };
            if (result.empty() == false && result.back() != u8'\\' && result.back() != u8'/')
                result.push_back(u8'\\');
            result.append(right);
            return result;
        }
    } // namespace

    file_log_writer::file_log_writer(log_file_system& file_system)
        : file_system_ { &file_system }
    {
        thread_ = std::thread { &file_log_writer::writer_main, this };
    }

    file_log_writer::~file_log_writer()
    {
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            stopping_ = true;
        }
        pending_.notify_all();
        if (thread_.joinable())
            thread_.join();
    }

    void file_log_writer::set_document(const std::u8string_view document_path, const std::span<const log_file_target> targets)
    {
        // 폴더 이름은 문서 안의 저장소 전체를 함께 봐야 정해진다 (A4.1).
        std::vector<std::u8string> paths {};
        paths.reserve(targets.size());
        for (const log_file_target& target : targets)
            paths.push_back(target.repository_path);
        const std::vector<std::u8string> folders { log_folder_names(paths) };

        generation_state state {};
        state.root = document_path.empty() ? std::u8string {} : log_root_path(document_path);
        for (std::size_t index = 0; index < targets.size(); ++index)
        {
            target_state entry {};
            entry.folder = index < folders.size() ? folders[index] : targets[index].id.value;
            entry.display_name = targets[index].display_name;
            entry.repository_path = targets[index].repository_path;
            state.targets.emplace(targets[index].id.value, std::move(entry));
        }

        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            ++document_generation_;
            generations_.emplace(document_generation_, std::move(state));
            prune_generations();
        }
        pending_.notify_all();
    }

    void file_log_writer::append(const project_id& id, const operation_log_entry& entry)
    {
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            if (root_disabled())
                return;

            if (queue_.size() >= log_file_queue_capacity)
            {
                // 가장 오래된 것부터 버린다. 디스크가 느려도 logic은 멈추지 않는다.
                queue_.erase(queue_.begin());
                ++dropped_;
                ++unreported_drops_;
            }
            queue_.push_back(queued_entry { document_generation_, id, entry });
        }
        pending_.notify_one();
    }

    void file_log_writer::flush()
    {
        std::unique_lock<std::mutex> lock { mutex_ };
        drained_.wait(lock, [this] { return (queue_.empty() && writing_ == false) || stopping_; });
    }

    std::optional<std::u8string> file_log_writer::take_failure()
    {
        const std::lock_guard<std::mutex> lock { mutex_ };
        std::optional<std::u8string> failure { std::move(failure_) };
        failure_.reset();
        return failure;
    }

    std::size_t file_log_writer::dropped() const noexcept
    {
        const std::lock_guard<std::mutex> lock { mutex_ };
        return dropped_;
    }

    bool file_log_writer::disabled() const noexcept
    {
        const std::lock_guard<std::mutex> lock { mutex_ };
        const auto current { generations_.find(document_generation_) };
        return current != generations_.end() && current->second.disabled;
    }

    bool file_log_writer::root_disabled() const
    {
        const auto current { generations_.find(document_generation_) };
        return current == generations_.end() || current->second.root.empty() || current->second.disabled;
    }

    void file_log_writer::prune_generations()
    {
        for (auto entry = generations_.begin(); entry != generations_.end();)
        {
            if (entry->first == document_generation_)
            {
                ++entry;
                continue;
            }

            bool queued { false };
            for (const queued_entry& value : queue_)
                if (value.document == entry->first)
                    queued = true;
            entry = queued ? std::next(entry) : generations_.erase(entry);
        }
    }

    void file_log_writer::writer_main()
    {
        while (true)
        {
            std::vector<queued_entry> batch {};
            {
                std::unique_lock<std::mutex> lock { mutex_ };
                pending_.wait(lock, [this] { return queue_.empty() == false || stopping_; });
                if (queue_.empty() && stopping_)
                {
                    drained_.notify_all();
                    return;
                }
                batch.swap(queue_);
                writing_ = true;
            }

            for (const queued_entry& entry : batch)
                write_entry(entry);

            {
                const std::lock_guard<std::mutex> lock { mutex_ };
                writing_ = false;
                prune_generations();
            }
            drained_.notify_all();
        }
    }

    void file_log_writer::write_entry(const queued_entry& entry)
    {
        std::u8string path {};
        std::u8string text {};
        {
            std::unique_lock<std::mutex> lock { mutex_ };
            const auto generation { generations_.find(entry.document) };
            if (generation == generations_.end() || generation->second.disabled || generation->second.root.empty())
                return;

            const auto target { generation->second.targets.find(entry.id.value) };
            if (target == generation->second.targets.end())
                return;

            if (target->second.file_path.empty())
            {
                // 파일과 폴더는 첫 기록에서 만든다. 로그가 없는 저장소는 폴더도
                // 만들지 않는다 (A4.2).
                const std::u8string folder { join_path(generation->second.root, target->second.folder) };
                lock.unlock();
                const bool created { file_system_->create_directories(folder) };
                lock.lock();
                if (created == false)
                {
                    generation->second.disabled = true;
                    failure_ = u8"로그 폴더를 만들지 못해 이 문서의 파일 로그를 끕니다: " + folder;
                    return;
                }

                std::u8string file {};
                for (std::size_t attempt = 0; attempt < file_name_attempts; ++attempt)
                {
                    std::u8string candidate { join_path(folder, log_file_name(std::chrono::system_clock::now(), attempt)) };
                    lock.unlock();
                    const bool exists { file_system_->file_exists(candidate) };
                    lock.lock();
                    if (exists == false)
                    {
                        file = std::move(candidate);
                        break;
                    }
                }
                if (file.empty())
                {
                    generation->second.disabled = true;
                    failure_ = u8"로그 파일 이름을 정하지 못해 이 문서의 파일 로그를 끕니다.";
                    return;
                }

                target->second.file_path = file;
                // 머리글 한 줄로 어떤 문서·저장소의 로그인지 남긴다 (A4.3).
                text += u8"# 문서: ";
                text += generation->second.root;
                text += line_break;
                text += u8"# 저장소: ";
                text += target->second.display_name;
                text += u8" (";
                text += target->second.repository_path;
                text += u8")";
                text += line_break;
            }

            if (unreported_drops_ > 0)
            {
                text += u8"# 앞선 로그 ";
                text += dropped_text(unreported_drops_);
                text += line_break;
                unreported_drops_ = 0;
            }

            path = target->second.file_path;
            text += format_log_file_line(entry.record);
            text += line_break;
        }

        if (file_system_->append_file(path, text))
            return;

        const std::lock_guard<std::mutex> lock { mutex_ };
        const auto generation { generations_.find(entry.document) };
        if (generation == generations_.end())
            return;
        generation->second.disabled = true;
        failure_ = u8"로그 파일을 쓰지 못해 이 문서의 파일 로그를 끕니다: " + path;
    }
} // namespace gitman
