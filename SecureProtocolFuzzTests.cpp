#include "SecureActuation.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    try {
        for (int length = 0; length <= 2049; length += 17) {
            std::string payload(static_cast<size_t>(length), 'A');
            if (!payload.empty()) payload[0] = static_cast<char>(length & 0x7F);
            SecureProtocolMessage message;
            std::string reason;
            const bool accepted = SecureMessageValidation::Parse(payload, message, reason);
            if (accepted) throw std::runtime_error("unstructured fuzz payload accepted");
        }
        const char* malformed[] = {
            "v=1|id=x|session=s|seq=0|issued=1|expires=2|cmd=BEGIN_CANARY|proof=p",
            "v=1|id=x|session=s|seq=1|issued=2|expires=1|cmd=BEGIN_CANARY|proof=p",
            "v=1|id=x|session=s|seq=1|issued=1|expires=2|cmd=ARBITRARY_COMMAND|proof=p",
            "v=99|id=x|session=s|seq=1|issued=1|expires=2|cmd=BEGIN_CANARY|proof=p",
        };
        for (const char* payload : malformed) {
            SecureProtocolMessage message;
            std::string reason;
            if (SecureMessageValidation::Parse(payload, message, reason)) throw std::runtime_error("malformed protocol accepted");
        }
        std::cout << "SecureProtocolFuzzTests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
