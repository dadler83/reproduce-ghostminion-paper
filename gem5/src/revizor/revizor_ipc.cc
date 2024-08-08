#include "revizor/revizor_ipc.hh"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <iomanip>
#include <string_view>
#include <utility>

#include "base/logging.hh"
//#include "base/trace.hh"
#include "debug/RevizorCommands.hh"
#include "debug/RevizorExec.hh"
#include "debug/RevizorResults.hh"
#include "mem/abstract_mem.hh"
#include "revizor/inspectable_cache.hh"
#include "sim/process.hh"
//#include "sim/workload.hh"
//#include "sim/se_workload.hh"
//#include "sim/mem_pool.hh"

namespace gem5
{

static constexpr uint64_t maxCodeSize = 4096;
static const uint64_t maxSandboxSize = 8192;
static const uint64_t maxRegistersSize = 30 * 8;
static const uint64_t opInit = 0xd09e95bc2c73ad66;
static const uint64_t opAckInit = 0xc4f991d25774a0ac;
static const uint64_t opLoadTestCase = 0xf06e27858611c27a;
static const uint64_t opAckLoadTestCase = 0x847431e37076fb26;
static const uint64_t opTraceTestCase = 0x9ca711a73355bea;
static const uint64_t opAckTraceTestCase = 0xc1f8bc29862ef946;
static const uint64_t opResetLog = 0x7e310c4276780c9b;
static const uint64_t opQuit = 0x8492384098c80892;

void RevizorIPC::recv(void *buf, size_t size) {
    uint8_t *b = (uint8_t *)buf;
    size_t read = 0;
    while (read < size) {
        ssize_t count = ::read(sock, b + read, size - read);
        if (count > 0) {
            read += count;
        } else if (count == 0) {
            fatal("unexpected EOF from client\n");
        } else {
            fatal("error reading from client: %s\n",
                strerror(errno));
        }
    }
}

void RevizorIPC::send(const void *buf, size_t size) {
    const uint8_t *b = (const uint8_t *)buf;
    size_t written = 0;
    while (written < size) {
        ssize_t count = ::write(sock, b + written, size - written);
        if (count > 0) {
            written += count;
        } else {
            fatal("error writing to client: %s\n", strerror(errno));
        }
    }
}

typedef std::unordered_map<std::string, uint64_t> SymbolAddresses;

static SymbolAddresses getSymbolAddresses(const char *executable_path) {
    FILE *fp = fopen(executable_path, "rb");
    if (!fp) {
        fatal("couldn't open executable %s: %s\n",
            executable_path, strerror(errno));
    }
    struct ElfHeader
    {
        uint8_t identifier[7];
        uint8_t osabi, abiversion;
        uint8_t pad[7];
        uint16_t type;
        uint16_t machine;
        uint32_t version;
        uint64_t entry;
        uint64_t phoff;
        uint64_t shoff;
        uint32_t flags;
        uint16_t ehsize;
        uint16_t phentsize;
        uint16_t phnum;
        uint16_t shentsize;
        uint16_t shnum;
        uint16_t shstrndx;
    };
    struct ElfSectionHeader
    {
        uint32_t name;
        uint32_t type;
        uint64_t flags;
        uint64_t addr;
        uint64_t offset;
        uint64_t size;
        uint32_t link;
        uint32_t info;
        uint64_t addralign;
        uint64_t entsize;
    };
    struct ElfSymbol
    {
        uint32_t name;
        unsigned char info;
        unsigned char other;
        uint16_t shndx;
        uint64_t value;
        uint64_t size;
    };
    static_assert(sizeof(ElfHeader) == 0x40);
    static_assert(sizeof(ElfSectionHeader) == 0x40);
    const uint8_t expected_identifier[7] = {
        0x7f, 'E', 'L', 'F',
        2, // 64-bit
        1, // little-endian
        1 // ELF version 1
    };
    ElfHeader header = {};
    fread(&header, 1, sizeof header, fp);
    if (memcmp(header.identifier,
        expected_identifier, sizeof header.identifier) != 0) {
        fatal("%s is not a 64-bit little endian ELF executable\n",
            executable_path);
    }
    uint32_t shstrndx = header.shstrndx;
    fseek(fp, (long)(shstrndx * header.shentsize + header.shoff), SEEK_SET);
    ElfSectionHeader shstrtab_header = {};
    fread(&shstrtab_header, sizeof shstrtab_header, 1, fp);
    uint64_t shstrtab_offset = shstrtab_header.offset;
    uint64_t symtab_offset = 0;
    uint64_t symtab_size = 0;
    std::vector<char> strtab;
    for (uint32_t sh = 0; sh < header.shnum; sh++) {
        fseek(fp, (long)(sh * header.shentsize + header.shoff), SEEK_SET);
        ElfSectionHeader section_header = {};
        fread(&section_header, sizeof section_header, 1, fp);
        char name[16] = {};
        fseek(fp, (long)(shstrtab_offset + section_header.name), SEEK_SET);
        for (int i = 0; i < sizeof name - 1; i++) {
            int c = getc(fp);
            if (c == 0 || c == EOF) break;
            name[i] = c;
        }
        if (strcmp(name, ".strtab") == 0) {
            strtab.resize(section_header.size, 0);
            fseek(fp, (long)section_header.offset, SEEK_SET);
            fread(&strtab[0], 1, section_header.size, fp);
        }
        if (strcmp(name, ".symtab") == 0) {
            symtab_offset = section_header.offset;
            symtab_size = section_header.size;
            assert(section_header.entsize == sizeof (ElfSymbol));
        }
    }
    fseek(fp, (long)symtab_offset, SEEK_SET);
    SymbolAddresses addresses = {};
    for (uint32_t i = 0; i < symtab_size / sizeof (ElfSymbol); i++) {
        ElfSymbol symbol = {};
        fread(&symbol, sizeof symbol, 1, fp);
        const char *name = &strtab.at(symbol.name);
        addresses[name] = symbol.value;
    }
    fclose(fp);
    const char *const required_symbols[] = {
        "code",
        "sandbox",
        "registers",

        NULL,
    };
    for (size_t i = 0; required_symbols[i]; i++) {
        if (addresses.count(required_symbols[i]) == 0) {
            fatal("%s does not define symbol `%s`\n", required_symbols[i]);
        }
    }
    return addresses;
}

RevizorIPC::RevizorIPC(RevizorIPCParams *params) :
    SimObject(params),
    l1dCache(params->l1d_cache),
    l1iCache(params->l1i_cache),
    l2Cache(params->l2_cache),
    cpu(params->cpu),
    dram(params->dram),
    process(params->process),
    addresses(getSymbolAddresses(
        params->executable_path.c_str()))
{
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        fatal("couldn't create socket: %s\n", strerror(errno));
    }
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    const char *socket_name = params->socket_name.c_str();
    if (strlen(socket_name) + 1 > sizeof addr.sun_path) {
        fatal("socket path is too long.");
    }
    // first byte is left as 0 to indicate an abstract-domain socket
    memcpy(&addr.sun_path[1], socket_name, strlen(socket_name));
    if (connect(sock, (const struct sockaddr *)&addr,
             sizeof(sa_family_t) + 1 + strlen(socket_name)) == -1) {
        fatal("couldn't connect to server: %s\n", strerror(errno));
    }
    uint64_t init = 0;
    recv(&init, sizeof init);
    if (init != opInit) {
        fatal("client didn't send init operation: got %#" PRIx64 "\n", init);
    }
    DPRINTF(RevizorCommands, "Got command: init\n");
    const uint64_t ackInit[3] = {
        opAckInit,
        addresses.at("sandbox"),
        addresses.at("code"),
    };
    DPRINTF(RevizorCommands, "Send command: acknowledge init\n");
    send(ackInit, sizeof ackInit);
}

RevizorIPC::~RevizorIPC() {
    close(sock);
}

void RevizorIPC::startup() {
}

uint8_t *RevizorIPC::vaddrToHost(Addr vaddr) {
    Addr paddr = 0;
    process->pTable->translate(vaddr, paddr);
    return dram->toHostAddr(paddr);
}

void RevizorIPC::loadTestCase() {
    uint64_t testCaseSize = 0;
    recv(&testCaseSize, sizeof testCaseSize);
    DPRINTF(RevizorCommands, "Got command: Load test case (size %llu)\n",
        (unsigned long long)testCaseSize);
    if (testCaseSize > maxCodeSize) {
        fatal("test case is too large: %" PRIu64 " bytes\n", testCaseSize);
    }
    uint8_t code[maxCodeSize];
    // loader::Arch arch = process->seWorkload->getArch();
    // fill unused portion of code with noops
    // switch (arch) {
    // case loader::Arch::X86_64:
    // case loader::Arch::I386:
    //     memset(code, 0x90, sizeof code);
    //     break;
    // case loader::Arch::Arm64:
        for (size_t i = 0; i < maxCodeSize; i += 4) {
            const uint32_t noop = 0xD503201F;
            memcpy(&code[i], &noop, 4);
        }
    //     break;
    // default:
    //     fatal("unrecognized architecture: %s\n", loader::archToString(arch));
    // }
    recv(code, testCaseSize);
    const uint64_t codeAddress = addresses.at("code");
    for (size_t i = 0; i < maxCodeSize; i++) {
        // we don't really need to do translation for every address
        // but it's not too slow for now
        *vaddrToHost(codeAddress + i) = code[i];
    }
    DPRINTF(RevizorExec, "--- Code ---\n");
    for (size_t i = 0; i < (testCaseSize + 7) / 8; i++) {
        std::stringstream line;
        line << std::hex << codeAddress + 8 * i;
        for (size_t byte = 0; byte < 8; byte++) {
            uint64_t addr = codeAddress + 8 * i + byte;
            if (addr >= codeAddress + testCaseSize) {
                break;
            }
            line << std::setfill('0') << std::setw(2)
                << (unsigned int)*vaddrToHost(addr);
            line << " ";
        }
        std::string line_string = line.str();
        DPRINTF(RevizorExec, "%s\n", line_string.c_str());
    }

    loadedTestCase = true;
    uint64_t response[] = {
        opAckLoadTestCase,
    };
    send(response, sizeof response);
}

static uint64_t getInputHash(const std::vector<uint8_t> &input) {
    uint64_t hash = 0xbb7524eafb93804b;
    for (uint8_t x: input) {
        hash += x;
        hash *= 0x21f782547ea34f3d;
    }
    return hash;
}

void RevizorIPC::traceTestCase() {
    l1dCache->writeback();
    l1iCache->writeback();
    l2Cache->writeback();
    uint64_t metadata[3] = {0};
    recv(metadata, sizeof metadata);
    const uint64_t inputSize = metadata[0];
    const uint64_t registersStart = metadata[1];
    inputHash = metadata[2];
    DPRINTF(RevizorCommands,
        "Got command: trace test case input"
        "hash=%016llx size=%llu registers=%llu\n",
        (unsigned long long)inputHash,
        (unsigned long long)inputSize,
        (unsigned long long)registersStart);
    std::vector<uint8_t> input;
    input.resize(inputSize, 0);
    recv(&input[0], inputSize);
    assert(registersStart <= maxSandboxSize);
    assert(inputSize - registersStart <= maxRegistersSize);
    assert(getInputHash(input) == inputHash);
    uint8_t *sandbox = vaddrToHost(addresses.at("sandbox"));
    uint8_t *registers = vaddrToHost(addresses.at("registers"));
    memset(sandbox, 0, maxSandboxSize);
    memcpy(sandbox, &input[0], registersStart);
    memset(registers, 0, maxRegistersSize);
    memcpy(registers, &input[registersStart], inputSize - registersStart);
    l1dCache->invalidate();
    l1iCache->invalidate();
    l2Cache->invalidate();
}

void RevizorIPC::analyze() {
    const std::vector<bool> valid = l1dCache->validSet();
    const uint64_t cacheBlockCount = valid.size();
    std::vector<uint8_t> data = {};
    data.resize(24 + cacheBlockCount, 0);
    memcpy(&data[0], &opAckTraceTestCase, 8);
    memcpy(&data[8], &cacheBlockCount, 8);
    memcpy(&data[16], &inputHash, 8);
    for (uint64_t block = 0; block < cacheBlockCount; block++) {
        data[24 + block] = valid.at(block);
    }
    DPRINTF(RevizorCommands,
        "Send command: test case results for input %016llx. size = %llu\n",
        (unsigned long long)inputHash,
        (unsigned long long)cacheBlockCount);
    send(&data[0], data.size());
    DPRINTF(RevizorResults, "---- input #%llu: %016llx ----\n",
        (unsigned long long)inputIndex,
        (unsigned long long)inputHash);
    inputIndex += 1;
    l1dCache->debug();
}

bool RevizorIPC::prepareNext() {
    if (loadedTestCase) {
        analyze();
    }
    while (true) {
        uint64_t op = 0;
        recv(&op, sizeof op);
        if (op == opQuit) {
            DPRINTF(RevizorCommands, "Got command: quit\n");
            return false;
        } else if (op == opLoadTestCase) {
            loadTestCase();
        } else if (op == opTraceTestCase) {
            traceTestCase();
            return true;
        } else if (op == opResetLog) {
            Trace::getDebugLogger()->reset();
        } else {
            fatal("unrecognized command from client: %#" PRIx64 "\n", op);
        }
    }
}


} // namespace gem5

gem5::RevizorIPC *RevizorIPCParams::create()
{
    return new gem5::RevizorIPC(this);
}
