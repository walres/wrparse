#ifndef WR_PARSE_DIAGNOSTICS_H
#define WR_PARSE_DIAGNOSTICS_H

#include <iosfwd>
#include <string>
#include <wrutil/circ_fwd_list.h>
#include <wrutil/Format.h>
#include <wrparse/Token.h>


namespace wr {
namespace parse {


class WRPARSE_API Diagnostic
{
public:
        enum Category { INFO, WARNING, ERROR, FATAL_ERROR };

        template <typename ...Args>
        Diagnostic(Category category, Token::Offset offset, unsigned int bytes,
                   Line line, Column column, const char *fmt, Args ...args) :
                category_(category),
                offset_  (offset),
                bytes_   (bytes),
                line_    (line),
                column_  (column),
                text_    (printStr(fmt, args...))
        {
        }

        template <typename ...Args>
        Diagnostic(Category category, const Token &token,
                   const char *fmt, Args ...args) :
                category_(category),
                offset_  (token.offset()),
                bytes_   (token.bytes()),
                line_    (token.line()),
                column_  (token.column()),
                text_    (printStr(fmt, args...))
        {
        }

        template <typename ...Args>
        Diagnostic(Category category, const Token &first_token,
                   const Token &last_token, const char *fmt, Args ...args) :
                category_(category),
                offset_  (first_token.offset()),
                bytes_   (last_token.offset() - first_token.offset()
                                              + last_token.bytes()),
                line_    (first_token.line()),
                column_  (first_token.column()),
                text_    (printStr(fmt, args...))
        {
        }

        Category category() const noexcept    { return category_; }
        Token::Offset offset() const noexcept { return offset_; }
        unsigned int bytes() const noexcept   { return bytes_; }
        Line line() const noexcept            { return line_; }
        Column column() const noexcept        { return column_; }
        string_view text() const noexcept     { return text_; }

        const char *describeCategory() const noexcept
                { return describe(category_); }

        static const char *describe(Category category);

private:
        Category      category_;
        Token::Offset offset_;
        unsigned int  bytes_;
        Line          line_;
        Column        column_;
        std::string   text_;
};

//--------------------------------------

class WRPARSE_API DiagnosticHandler
{
public:
        virtual ~DiagnosticHandler() noexcept;
        virtual void onDiagnostic(const Diagnostic &d);
};

//--------------------------------------

class WRPARSE_API DiagnosticCounter :
        public DiagnosticHandler
{
public:
        DiagnosticCounter();

        virtual void onDiagnostic(const Diagnostic &d) override;

        size_t totalCount() const noexcept;
        size_t infoCount() const noexcept { return info_count_; }
        size_t warningCount() const noexcept { return warning_count_; }
        size_t errorCount() const noexcept;

        size_t nonFatalErrorCount() const noexcept
                { return nonfatal_error_count_; }

        size_t fatalErrorCount() const noexcept
                { return fatal_error_count_; }

        void reset();

private:
        size_t info_count_,
               warning_count_,
               nonfatal_error_count_,
               fatal_error_count_;
};

//--------------------------------------

class WRPARSE_API DiagnosticEmitter
{
public:
        virtual ~DiagnosticEmitter() noexcept;

        /**
         * \brief Add receiver of diagnostic messages
         * \param [in] handler  reference to receiver object
         */
        void addDiagnosticHandler(DiagnosticHandler &handler);

        /**
         * \brief Remove a receiver of diagnostic messages
         * \param [in] handler  reference to receiver object to be removed
         * \return `true` if successfully removed, `false` if not found
         */
        bool removeDiagnosticHandler(DiagnosticHandler &handler);

        /// \brief Emit diagnostic message `d`
        void emit(const Diagnostic &d);

        /**
         * \brief Inquire whether any handlers are registered
         * \return `true` if no handlers registered, `false` otherwise
         */
        bool empty() const { return handlers_.empty(); }

private:
        circ_fwd_list<DiagnosticHandler *> handlers_;
};


} // namespace parse
} // namespace wr


#endif // !WR_PARSE_DIAGNOSTICS_H
