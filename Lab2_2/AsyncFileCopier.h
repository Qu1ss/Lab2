#pragma once
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0A00
#endif
#include <boost/asio.hpp>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>
#include "CopyTypes.h"

class AsyncFileCopier {
public:
    explicit AsyncFileCopier(std::size_t blockSize) : blockSize_(blockSize) {
        if (blockSize == 0) {
            throw std::invalid_argument("Размер блока должен быть положительным");
        }
    }

    boost::asio::awaitable<FileCopyResult> copy(const FileTask& task) const {
        namespace asio = boost::asio;
        std::uint64_t total = 0;
        try {
            const auto executor = co_await asio::this_coro::executor;
            boost::system::error_code ec;

            asio::stream_file in(executor);
            in.open(task.source.string(), asio::stream_file::read_only, ec);
            if (ec) {
                co_return failedResult(task, total, "не удалось открыть исходный файл" + errorCodeText(ec.value()));
            }

            asio::stream_file out(executor);
            out.open(task.target.string(),
                     asio::stream_file::write_only | asio::stream_file::create | asio::stream_file::truncate, ec);
            if (ec) {
                co_return failedResult(task, total, "не удалось создать целевой файл" + errorCodeText(ec.value()));
            }

            std::vector<char> buffer(blockSize_);
            while (true) {
                const std::size_t got = co_await in.async_read_some(
                    asio::buffer(buffer), asio::redirect_error(asio::use_awaitable, ec));
                if (got > 0) {
                    boost::system::error_code writeEc;
                    co_await asio::async_write(out, asio::buffer(buffer.data(), got),
                                               asio::redirect_error(asio::use_awaitable, writeEc));
                    if (writeEc) {
                        co_return failedResult(task, total, "ошибка записи" + errorCodeText(writeEc.value()));
                    }
                    total += got;
                }
                if (ec == asio::error::eof) {
                    break;
                }
                if (ec) {
                    co_return failedResult(task, total, "ошибка чтения" + errorCodeText(ec.value()));
                }
            }

            out.close(ec);
            if (ec) {
                co_return failedResult(task, total, "ошибка закрытия файла" + errorCodeText(ec.value()));
            }
        } catch (const std::exception&) {
            co_return failedResult(task, total, "внутренняя ошибка при копировании");
        }
        co_return succeededResult(task, total);
    }

private:
    std::size_t blockSize_;
};
