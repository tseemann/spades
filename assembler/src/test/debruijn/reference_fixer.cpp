#include "standard_base.hpp"
#include "simple_tools.hpp"
#include "logger/log_writers.hpp"

#include "path_helper.hpp"
#include "io/file_reader.hpp"
#include "io/wrapper_collection.hpp"
#include "io/osequencestream.hpp"

void create_console_logger() {
    logging::logger *log = logging::create_logger("", logging::L_INFO);
    log->add_writer(std::make_shared<logging::console_writer>());
    logging::attach_logger(log);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        cout << "Usage: reference_fixer <reference path> <output path>" << endl;
        return 1;
    }
    create_console_logger();
    string fn = argv[1];
    path::CheckFileExistenceFATAL(fn);
    string out_fn = argv[2];
    auto reader = make_shared<io::NonNuclCollapsingWrapper>(make_shared<io::FileReadStream>(fn));
    io::SingleRead read;
    std::stringstream ss;
    while (!reader->eof()) {
        (*reader) >> read;
        ss << read.GetSequenceString();
    }
    io::SingleRead concat("concat", ss.str());
    io::osequencestream out(out_fn);
    out << concat;
    return 0;
}
