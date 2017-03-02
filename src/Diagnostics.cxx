#include <limits>
#include <wrparse/Diagnostics.h>


namespace wr {
namespace parse {


const char *
Diagnostic::describe(
        Category category
) // static
{
        switch (category) {
        case INFO:
                return "note";
        case WARNING:
                return "warning";
        case ERROR:
                return "error";
        case FATAL_ERROR:
                return "fatal error";
        default:
                return "unknown category";
        }
}

//--------------------------------------

WRPARSE_API DiagnosticHandler::~DiagnosticHandler() noexcept = default;

//--------------------------------------

WRPARSE_API void
DiagnosticHandler::onDiagnostic(
        const Diagnostic &
)
{
        // default no-op
}

//--------------------------------------

WRPARSE_API
DiagnosticCounter::DiagnosticCounter()
{
        reset();
}

//--------------------------------------

WRPARSE_API void
DiagnosticCounter::onDiagnostic(
        const Diagnostic &d
)
{
        switch (d.category()) {
        case Diagnostic::INFO:
                ++info_count_;
                break;
        case Diagnostic::WARNING:
                ++warning_count_;
                break;
        case Diagnostic::ERROR:
                ++nonfatal_error_count_;
                break;
        case Diagnostic::FATAL_ERROR:
                ++fatal_error_count_;
                break;
        default:
                break;
        }
}

//--------------------------------------

WRPARSE_API size_t
DiagnosticCounter::totalCount() const noexcept
{
        return info_count_ + warning_count_
                           + nonfatal_error_count_ + fatal_error_count_;
}

//--------------------------------------

WRPARSE_API size_t
DiagnosticCounter::errorCount() const noexcept
{
        return nonfatal_error_count_ + fatal_error_count_;
}

//--------------------------------------

WRPARSE_API void
DiagnosticCounter::reset()
{
        info_count_ = warning_count_
                    = nonfatal_error_count_ = fatal_error_count_ = 0;
}

//--------------------------------------

WRPARSE_API DiagnosticEmitter::~DiagnosticEmitter() noexcept = default;

//--------------------------------------

WRPARSE_API void
DiagnosticEmitter::addDiagnosticHandler(
        DiagnosticHandler &handler
)
{
        handlers_.push_back(&handler);
}

//--------------------------------------

WRPARSE_API bool
DiagnosticEmitter::removeDiagnosticHandler(
        DiagnosticHandler &handler
)
{
        auto i = handlers_.before_begin(), j = handlers_.last();

        while (i != j) {
                auto next = std::next(i);
                if (*next == &handler) {
                        handlers_.erase_after(i);
                        return true;
                }
                i = next;
        }

        return false;
}

//--------------------------------------

WRPARSE_API void
DiagnosticEmitter::emit(
        const Diagnostic &d
)
{
        for (auto h: handlers_) {
                h->onDiagnostic(d);
        }
}


} // namespace parse
} // namespace wr
