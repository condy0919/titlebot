#ifndef TITLEBOT_IRC_HPP_
#define TITLEBOT_IRC_HPP_

#include <string>

namespace tb {
/// Remove colors from IRC texts.
std::string normalize(const std::string& s);
}

#endif // TITLEBOT_IRC_HPP_
