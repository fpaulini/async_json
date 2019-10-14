/* ==========================================================================
 Copyright (c) 2019 Andreas Pokorny
 Distributed under the Boost Software License, Version 1.0. (See accompanying
 file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
========================================================================== */

#ifndef ASYNC_JSON_BASIC_JSON_PARSER_HPP_INCLUDED
#define ASYNC_JSON_BASIC_JSON_PARSER_HPP_INCLUDED
#include <cmath>
#include <hsm/hsm.hpp>

// get string view:
#if defined(__clang__)

#if __cplusplus >= 201500L
#include <string_view>
#else
#include <experimental/string_view>
namespace std
{
using string_view = experimental::basic_string_view<char>;
}
#endif

#elif defined(__GNUC__)

#if __cplusplus >= 201703L

#include <string_view>

#elif (__GNUC__ == 4 && __GNUC_MINOR >= 9) || (__GNUC__ >= 5)

#include <experimental/string_view>
namespace std
{
using string_view = experimental::basic_string_view<char>;
}
#else

#error "please use a newer compiler that contains string_view"
// namespace std
#endif

#else

#include <string_view>

#endif

namespace async_json
{
namespace detail
{
constexpr hsm::event<struct character_event> ch;
constexpr hsm::event<struct digit_char>      digit;
constexpr hsm::event<struct quot_char>       quot;
constexpr hsm::event<struct colon_char>      colon;
constexpr hsm::event<struct comma_char>      comma;
constexpr hsm::event<struct dot_char>        dot;
constexpr hsm::event<struct exponent_char>   exponent;
constexpr hsm::event<struct plus_char>       plus;
constexpr hsm::event<struct minus_char>      minus;
constexpr hsm::event<struct br_open_char>    br_open;
constexpr hsm::event<struct br_close_char>   br_close;
constexpr hsm::event<struct idx_open_char>   idx_open;
constexpr hsm::event<struct idx_close_char>  idx_close;
constexpr hsm::event<struct ws>              whitespace;
constexpr hsm::event<struct t_char>          t;
constexpr hsm::event<struct f_char>          f;
constexpr hsm::event<struct n_char>          n;
constexpr hsm::event<struct end_of_input>    eoi;
}  // namespace detail

enum error_cause
{
    no_error                = 0,
    wrong_keyword_character = 1,
    mismatched_array,
    mismatched_brace,
    member_exp,
    colon_exp,
    unexpected_character,
    invalid_number,
    comma_expected
};

struct default_traits
{
    using float_t   = double;
    using integer_t = long;
    using sv_t      = std::string_view;
};

template <typename Traits>
struct default_handler
{
    using sv_t      = typename Traits::sv_t;
    using integer_t = typename Traits::integer_t;
    using float_t   = typename Traits::float_t;
    void value(bool) {}
    void value(void*) {}
    void value(integer_t) {}
    void value(float_t) {}
    void value(sv_t const&) {}
    void string_value_start(sv_t const&) {}
    void string_value_cont(sv_t const&) {}
    void string_value_end() {}
    void named_object(sv_t const&) {}
    void named_object_start(sv_t const&) {}
    void named_object_cont(sv_t const&) {}
    void named_object_end() {}
    void object_start() {}
    void object_end() {}
    void array_start() {}
    void array_end() {}
    void error(error_cause) {}
};

template <typename Handler = default_handler<default_traits>, typename Traits = default_traits>
struct basic_json_parser
{
    using float_t   = typename Traits::float_t;
    using integer_t = typename Traits::integer_t;
    using sv_t      = typename Traits::sv_t;

   private:
    Handler cbs;

    size_t byte_count{0};
    char   cur{0};

    // parsed states:
    struct keyword_receive
    {
        char const* kw{nullptr};
        uint8_t     pos{0};
    };
    keyword_receive kw_state;
    sv_t            parsed_view;
    sv_t            current_input_buffer;

    int                              num_sign{1};
    int                              exp_sign{1};
    int                              frac_digits{0};
    unsigned long long               exp_number{0};
    unsigned long long               int_number{0};
    unsigned long long               fraction{0};
    std::vector<uint8_t>             state_stack;
    std::function<bool(sv_t const&)> process_events;

    void setup_sm()
    {
        using namespace hsm::literals;
        auto* cb                  = this->callback_handler();
        auto  kw_consume          = [this]() { return cur == kw_state.kw[kw_state.pos] && 0 != kw_state.kw[kw_state.pos + 1]; };
        auto  setup_kw            = [this](char const* name) { return [this, name]() { kw_state = keyword_receive{name, 1}; }; };
        auto  kw_complete         = [this]() { return cur == kw_state.kw[kw_state.pos] && 0 == kw_state.kw[kw_state.pos + 1]; };
        auto  kw_wrong_char       = [this]() { return cur != kw_state.kw[kw_state.pos]; };
        auto  kw_consume_char     = [this]() { ++kw_state.pos; };
        auto  kw_complete_keyword = [cb, this]() {
            if (kw_state.kw == sv_t{"null"})
                cb->value(nullptr);
            else if (kw_state.kw == sv_t{"true"})
                cb->value(true);
            else
                cb->value(false);
        };

        auto negate      = [](auto& sign) { return [&sign]() { sign = -1; }; };
        auto add_digit   = [this](auto& num) { return [&num, this]() { num = num * 10 + (cur - '0'); }; };
        auto add_digit_c = [this](auto& num, auto& count) { return [&num, &count, this]() { ++count, num = num * 10 + (cur - '0'); }; };
        auto get_number  = [this]() {
            auto ret   = num_sign * static_cast<integer_t>(int_number);
            int_number = 0;
            num_sign   = 1;
            return ret;
        };
        auto get_fraction = [this]() {
            auto ret    = num_sign * (static_cast<integer_t>(int_number) + static_cast<float_t>(fraction) / std::pow(10, frac_digits));
            int_number  = 0;
            num_sign    = 1;
            fraction    = 0;
            frac_digits = 0;
            return ret;
        };
        auto get_fraction_we = [get_fraction, this]() {
            auto ret   = get_fraction() * std::pow(10, exp_sign * static_cast<integer_t>(exp_number));
            exp_number = 0;
            exp_sign   = 1;
            return ret;
        };

        auto emit_number       = [cb, get_number]() { cb->value(get_number()); };
        auto emit_fraction     = [cb, get_fraction]() { cb->value(get_fraction()); };
        auto emit_exp_fraction = [cb, get_fraction_we]() { cb->value(get_fraction_we()); };
        auto push_object       = [cb, this]() {
            cb->object_start();
            state_stack.push_back(0);
        };
        auto pop_object = [cb, this]() {
            cb->object_end();
            state_stack.pop_back();
        };
        auto push_array = [cb, this]() {
            cb->array_start();
            state_stack.push_back(1);
        };
        auto pop_array = [cb, this]() {
            cb->array_end();
            state_stack.pop_back();
        };
        auto emit_name_first = [cb, this]() {
            cb->named_object_start(parsed_view);
            parsed_view = sv_t(nullptr, 0);
        };

        auto emit_name_first_last = [cb, this]() {
            cb->named_object(parsed_view);
            parsed_view = sv_t(nullptr, 0);
        };

        auto emit_name_n = [cb, this]() {
            if (parsed_view.size()) cb->named_object_cont(parsed_view);
            parsed_view = sv_t(nullptr, 0);
        };

        auto emit_name_n_last = [cb, emit_name_n]() {
            emit_name_n();
            cb->named_object_end();
        };

        auto emit_str_first = [cb, this]() {
            cb->string_value_start(parsed_view);
            parsed_view = sv_t(nullptr, 0);
        };

        auto emit_str_first_last = [cb, this]() {
            cb->value(parsed_view);
            parsed_view = sv_t(nullptr, 0);
        };

        auto emit_str_n = [cb, this]() {
            if (parsed_view.size()) cb->string_value_cont(parsed_view);
            parsed_view = sv_t(nullptr, 0);
        };

        auto emit_str_n_last = [cb, emit_str_n]() {
            emit_str_n();
            cb->string_value_end();
        };

        auto error_action       = [cb](error_cause reason) { return [reason, cb]() { cb->error(reason); }; };
        auto stack_empty        = [this]() { return state_stack.size() == 0; };
        auto no_object_on_stack = [this]() { return state_stack.size() == 0 || state_stack.back() != 0; };
        auto object_on_stack    = [this]() { return state_stack.size() && state_stack.back() == 0; };
        auto no_array_on_stack  = [this]() { return state_stack.size() == 0 || state_stack.back() != 1; };
        auto array_on_stack     = [this]() { return state_stack.size() && state_stack.back() == 1; };
        auto mem_start_str      = [this]() { parsed_view = sv_t(current_input_buffer.begin(), 1); };
        auto mem_add_ch         = [this]() { parsed_view = sv_t(parsed_view.begin(), parsed_view.size() + 1); };

        using namespace async_json::detail;
        auto sm = hsm::create_state_machine(  //
            ch,                               // catch all event
            "done"_state,
            "error"_state,                                                      //
            hsm::initial = "json"_state,                                        //
            "json"_state(                                                       //
                whitespace                                    = hsm::internal,  //
                n / setup_kw("null")                          = "keyword"_state,
                f / setup_kw("false")                         = "keyword"_state,           //
                t / setup_kw("true")                          = "keyword"_state,           //
                br_open / push_object                         = "member"_state,            //
                idx_open / push_array                         = "json"_state,              //
                quot                                          = "string_start"_state,      //
                digit / add_digit(int_number)                 = "int_number"_state,        //
                minus / negate(num_sign)                      = "int_number_ws"_state,     //
                eoi                                           = hsm::internal,             //
                hsm::any / error_action(unexpected_character) = "error"_state              //
                ),                                                                         //
            "int_number"_state(                                                            //
                "int_number_ws"_state(                                                     //
                    whitespace                    = hsm::internal,                         //
                    digit / add_digit(int_number) = "int_number"_state),                   //
                digit / add_digit(int_number)           = hsm::internal,                   //
                dot                                     = "fraction_number"_state,         //
                exponent                                = "exponent_sign"_state,           //
                comma / emit_number                     = "array_object_comma"_state,      //
                br_close / emit_number                  = "array_object_br_close"_state,   //
                idx_close / emit_number                 = "array_object_idx_close"_state,  //
                whitespace / emit_number                = "array_object"_state,            //
                eoi                                     = hsm::internal,                   //
                hsm::any / error_action(invalid_number) = "error"_state                    //
                ),
            "fraction_number"_state(                                                          //
                digit / add_digit_c(fraction, frac_digits) = hsm::internal,                   //
                exponent                                   = "exponent_sign"_state,           //
                comma / emit_fraction                      = "array_object_comma"_state,      //
                br_close / emit_fraction                   = "array_object_br_close"_state,   //
                idx_close / emit_fraction                  = "array_object_idx_close"_state,  //
                whitespace / emit_fraction                 = "array_object"_state,            //
                eoi                                        = hsm::internal,                   //
                hsm::any / error_action(invalid_number)    = "error"_state                    //
                ),
            "exponent"_state(                                                    //
                digit / add_digit(exp_number) = hsm::internal,                   //
                "exponent_sign"_state(                                           //
                    minus / negate(exp_sign)                = "exponent"_state,  //
                    plus / negate(exp_sign)                 = "exponent"_state,  //
                    digit / add_digit(exp_number)           = "exponent"_state,  //
                    eoi                                     = hsm::internal,     //
                    hsm::any / error_action(invalid_number) = "error"_state      //
                    ),
                comma / emit_exp_fraction               = "array_object_comma"_state,      //
                br_close / emit_exp_fraction            = "array_object_br_close"_state,   //
                idx_close / emit_exp_fraction           = "array_object_idx_close"_state,  //
                whitespace / emit_exp_fraction          = "array_object"_state,            //
                eoi                                     = hsm::internal,                   //
                hsm::any / error_action(invalid_number) = "error"_state                    //
                ),
            "string_start"_state(  //
                hsm::any / mem_start_str = "string_start_cont"_state,
                "string_start_ch"_state(                                    //
                    hsm::any / mem_add_ch = hsm::internal,                  //
                    "string_start_cont"_state(                              //
                        quot / emit_str_first_last = "array_object"_state,  //
                        eoi / emit_str_first       = "string_n"_state)            //
                    )),
            "string_n"_state(                                     //
                quot / emit_str_n_last   = "array_object"_state,  //
                hsm::any / mem_start_str = "string_n_cont"_state,
                "string_n_ch"_state(                                    //
                    hsm::any / mem_add_ch = hsm::internal,              //
                    "string_n_cont"_state(                              //
                        quot / emit_str_n_last = "array_object"_state,  //
                        eoi / emit_str_n       = "string_n"_state))           //
                ),
            "member"_state(                                                         //
                hsm::initial = "expect_quot"_state,                                 //
                "expect_quot"_state(                                                //
                    whitespace                             = hsm::internal,         //
                    quot                                   = "name_start"_state,    //
                    br_close[object_on_stack] / pop_object = "array_object"_state,  //
                    hsm::any / error_action(member_exp)    = "error"_state),
                "name_start"_state(  //
                    hsm::any / mem_start_str = "name_start_cont"_state,
                    "name_start_ch"_state(                                       //
                        hsm::any / mem_add_ch = hsm::internal,                   //
                        "name_start_cont"_state(                                 //
                            quot / emit_name_first_last = "expect_colon"_state,  //
                            eoi / emit_name_first       = "name_n"_state))),           //
                "name_n"_state(                                                  //
                    quot / emit_name_n_last  = "expect_colon"_state,             //
                    hsm::any / mem_start_str = "name_n_cont"_state,
                    "name_n_ch"_state(                                       //
                        hsm::any / mem_add_ch = hsm::internal,               //
                        "name_n_cont"_state(                                 //
                            quot / emit_name_n_last = "expect_colon"_state,  //
                            eoi / emit_name_n       = "name_n"_state         //
                            )                                                //
                        )                                                    //
                    )                                                        //
                ),
            "expect_colon"_state(                                           //
                whitespace                         = "expect_colon"_state,  //
                colon                              = "json"_state,          //
                eoi                                = hsm::internal,         //
                hsm::any / error_action(colon_exp) = "error"_state),

            "keyword"_state(                                                                      //
                eoi                                                             = hsm::internal,  //
                hsm::any[kw_consume] / kw_consume_char                          = "keyword"_state,
                hsm::any[kw_complete] / kw_complete_keyword                     = "array_object"_state,  //
                hsm::any[kw_wrong_char] / error_action(wrong_keyword_character) = "error"_state          //
                ),
            "array_object"_state(                           //
                hsm::initial[stack_empty] = "done"_state,   //
                whitespace                = hsm::internal,  //
                eoi                       = hsm::internal,  //
                comma                     = "array_object_comma"_state,
                "array_object_comma"_state(                          //
                    hsm::initial[object_on_stack] = "member"_state,  //
                    hsm::initial[array_on_stack]  = "json"_state     //
                    ),
                br_close = "array_object_br_close"_state,
                "array_object_br_close"_state(                                                          //
                    hsm::initial[no_object_on_stack] / error_action(mismatched_brace) = "error"_state,  //
                    hsm::initial[object_on_stack] / pop_object                        = "array_object"_state),
                idx_close = "array_object_idx_close"_state,
                "array_object_idx_close"_state(                                                        //
                    hsm::initial[no_array_on_stack] / error_action(mismatched_array) = "error"_state,  //
                    hsm::initial[array_on_stack] / pop_array                         = "array_object"_state),
                hsm::any / error_action(comma_expected) = "error"_state  //
                ));
        sm.start();
        process_events = [this, sm = std::move(sm)](sv_t const& bytes) mutable {
            current_input_buffer = bytes;
            auto switch_char     = [&sm](auto c) {
                switch (c)
                {
                    case 'n': return sm.process_event(n);
                    case 't': return sm.process_event(t);
                    case 'f': return sm.process_event(f);
                    case '{': return sm.process_event(br_open);
                    case '}': return sm.process_event(br_close);
                    case '[': return sm.process_event(idx_open);
                    case ']': return sm.process_event(idx_close);
                    case '"': return sm.process_event(quot);
                    case ':': return sm.process_event(colon);
                    case ',': return sm.process_event(comma);
                    case '+': return sm.process_event(plus);
                    case '-': return sm.process_event(minus);
                    case '0': /* fall-through */
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                    case '8':
                    case '9': return sm.process_event(digit);
                    case 'E':
                    case 'e': return sm.process_event(exponent);
                    case '.': return sm.process_event(dot);
                    case ' ':
                    case '\t':
                    case '\r':
                    case '\n': return sm.process_event(whitespace);
                    default: return sm.process_event(ch);
                }
            };
            for (auto elem : bytes)
            {
                cur = elem;
                // TODO - validate utf8 characters
                switch_char(cur);
                if (sm.current_state_id() == sm.get_state_id("error"_state)) return false;
                ++byte_count;
                current_input_buffer = sv_t(current_input_buffer.begin() + 1, current_input_buffer.size() - 1);
            }
            sm.process_event(eoi);
            return sm.current_state_id() != sm.get_state_id("error"_state);
        };
    }

   public:
    explicit basic_json_parser(Handler&& handler) : cbs(std::move(handler)) { setup_sm(); }
    basic_json_parser() { setup_sm(); }

    Handler* callback_handler() { return &cbs; }
    bool     parse_bytes(sv_t const& input) { return process_events(input); }
};
}  // namespace async_json
#endif
