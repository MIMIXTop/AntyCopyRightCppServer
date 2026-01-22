#pragma once

namespace util {
template <typename... Lambdas>
struct match : Lambdas... {
    using Lambdas::operator()...;
};
template <typename... Lambdas>
match(Lambdas...) -> match<Lambdas...>;

}   // namespace util