#pragma once

#include <exception>
#include <string>
#include <utility>

namespace prism {
class SyntaxError : public std::exception {
  public:
    SyntaxError(std::string message) : m_message(std::move(message)) {
    }
    [[nodiscard]] const char* what() const noexcept override {
        return m_message.c_str();
    }

  private:
    std::string m_message;
};

class RuntimeError : public std::exception {
  public:
    RuntimeError(std::string message) : m_message(std::move(message)) {
    }
    const char* what() const noexcept override {
        return m_message.c_str();
    }

  private:
    std::string m_message;
};
} // namespace prism