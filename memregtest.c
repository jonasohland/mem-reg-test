#include <fcntl.h>
#include <getopt.h>
#include <mntent.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

// clang-format off
const struct option options[] = {
    {"help", 0, NULL, 'h'},
    {"info", 0, NULL, 'i'},
    {"provider", 1, NULL, 'p'},
    {"node", 1, NULL, 'n'},
    {"service", 1, NULL, 's'},
    {"mmap-flags", 1, NULL, 'M'},
    {"prot-flags", 1, NULL, 'P'},
    {"open-flags", 1, NULL, 'O'},
    {"caps", 1, NULL, 'c'},
    {"ep-type", 1, NULL, 'e'},
    {"mode", 1, NULL, 'm'},
    {NULL, 0, NULL, 0},
};
// clang-format on

void show_help(char const *argv0) {
    printf("usage: %s -n <node> -s <service>\n", argv0);
    printf(
        "options:\n"
        " -h, --help               Show this usage information\n"
        " -i, --info               Display information about the selected\n"
        "                          provider and fabric and then exit.\n"
        " -p, --provider <name>    Provider name hint, see fabric(7)\n"
        "                          for information.\n"
        " -n, --node <node>        Fabric node address, usually an ip address\n"
        "                          assigned to the rdma interface.\n"
        " -s, --service <service>  Service name/number, usually a port number\n"
        " -M, --mmap-flags <flags> Flags passed to mmap.\n"
        " -P, --prot-flags <flags> Protection flags passed to mmap.\n"
        " -O, --open-flags <flags> Flags passed to open()\n"
        " -c, --caps <caps>        Provider caps, see fi_info(7).\n"
        "                          Default: FI_RMA\n"
        " -e, --ep-type <type>     Endpoint type, default is FI_EP_MSG\n"
        " -m, --mode <mode>        Fabric mode, default: FI_RX_CQ_DATA\n");
}

void fi_perror(int status, char *msg) {
    fprintf(stderr, "%s: %s\n", msg, fi_strerror(status));
}

#define MAP_FLAG_STR(str, flag, flag_val)                                      \
    if (strcmp(str, #flag) == 0) {                                             \
        if (flag_val == NULL)                                                  \
            return -1;                                                         \
        *flag_val |= flag;                                                     \
        return 0;                                                              \
    }

int parse_flag_value(const char *string, int *flag_value_int,
                     uint64_t *flag_value_uint) {
    MAP_FLAG_STR(string, FI_WRITE, flag_value_uint);
    MAP_FLAG_STR(string, FI_RMA, flag_value_uint);
    MAP_FLAG_STR(string, FI_READ, flag_value_uint);
    MAP_FLAG_STR(string, FI_REMOTE_READ, flag_value_uint);
    MAP_FLAG_STR(string, FI_REMOTE_WRITE, flag_value_uint);
    MAP_FLAG_STR(string, FI_MSG, flag_value_uint);
    MAP_FLAG_STR(string, FI_EP_MSG, flag_value_uint);
    MAP_FLAG_STR(string, FI_EP_DGRAM, flag_value_uint);
    MAP_FLAG_STR(string, FI_EP_RDM, flag_value_uint);
    MAP_FLAG_STR(string, FI_EP_SOCK_STREAM, flag_value_uint);
    MAP_FLAG_STR(string, FI_RX_CQ_DATA, flag_value_uint);

    MAP_FLAG_STR(string, MAP_SHARED, flag_value_int);
    MAP_FLAG_STR(string, MAP_LOCKED, flag_value_int);

    MAP_FLAG_STR(string, PROT_READ, flag_value_int);
    MAP_FLAG_STR(string, PROT_WRITE, flag_value_int);
    MAP_FLAG_STR(string, PROT_NONE, flag_value_int);

    MAP_FLAG_STR(string, O_RDWR, flag_value_int);
    MAP_FLAG_STR(string, O_WRONLY, flag_value_int);
    MAP_FLAG_STR(string, O_RDONLY, flag_value_int);
    MAP_FLAG_STR(string, O_CLOEXEC, flag_value_int);

    return -1;
}

int parse_flag_values(char *arg, int *flag_value_int,
                      uint64_t *flag_value_uint) {
    char *tok = strtok(arg, ",|");
    while (tok != NULL) {
        if (parse_flag_value(tok, flag_value_int, flag_value_uint) < 0) {
            fprintf(stderr, "invalid flag: %s\n", tok);
            return -1;
        }

        tok = strtok(NULL, ",|");
    }

    return 0;
}

int main(int argc, char **argv) {
    struct fi_info *info = NULL;
    struct fi_info *hints = NULL;
    struct fid_fabric *fabric = NULL;
    struct fid_domain *domain = NULL;
    struct fid_mr *mr = NULL;
    struct iovec mr_iov;
    struct fi_mr_attr mr_attr;
    const char *shm_path = "/dev/shm/mr-reg-prot-test.shm";
    size_t shm_size = 1 << 16;
    int status;
    void *shm;

    // options
    int show_info = 0;

    char *node = NULL;
    char *service = NULL;

    char ch;
    int open_flags = 0;
    int mmap_prot = 0;
    int mmap_flags = 0;
    hints = fi_allocinfo();

    while ((ch = getopt_long(argc, argv, "hip:n:s:M:P:O:c:e:m:", options,
                             NULL)) != -1) {
        switch (ch) {
        case 'h':
            show_help(argv[0]);
            goto error;
        case 'i':
            show_info = 1;
            break;
        case 'p':
            hints->fabric_attr->prov_name = strdup(optarg);
            break;
        case 'n':
            node = strdup(optarg);
            break;
        case 's':
            service = strdup(optarg);
            break;
        case 'M':
            if (parse_flag_values(optarg, &mmap_flags, NULL) < 0)
                goto error;
            break;
        case 'P':
            if (parse_flag_values(optarg, &mmap_prot, NULL) < 0)
                goto error;
            break;
        case 'O':
            if (parse_flag_values(optarg, &open_flags, NULL) < 0)
                goto error;
            break;
        case 'c':
            if (parse_flag_values(optarg, NULL, &hints->caps) < 0)
                goto error;
            break;
        case 'e':
            if (parse_flag_values(optarg, NULL,
                                  (uint64_t *)&hints->ep_attr->type) < 0)
                goto error;
            break;
        case 'm':
            if (parse_flag_values(optarg, NULL, &hints->mode) < 0)
                goto error;
            break;
        }
    }

    if (service == NULL || node == NULL) {
        fprintf(stderr, "missing required <node> and <service> option");
        goto error;
    }

    if (!open_flags)
        open_flags = O_RDONLY;
    if (!mmap_flags)
        mmap_flags = MAP_SHARED | MAP_LOCKED;
    if (!mmap_prot)
        mmap_prot = PROT_READ;

    if (hints->fabric_attr->prov_name == NULL)
        hints->fabric_attr->prov_name = strdup("verbs");

    if (!hints->caps)
        hints->caps = FI_MSG | FI_REMOTE_WRITE;

    if (!hints->mode)
        hints->mode = FI_RX_CQ_DATA;

    if (!hints->ep_attr->type)
        hints->ep_attr->type = FI_EP_MSG;

    status = fi_getinfo(fi_version(), node, service, 0, NULL, &info);
    if (status < 0) {
        fi_perror(status, "get fabric info");
        goto error;
    }

    if (show_info) {
        printf("%s\n", fi_tostr(info, FI_TYPE_INFO));
        goto cleanup;
    }

    // prepare the shm file
    int fd = open(shm_path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 1) {
        perror("open");
        goto error;
    }

    if (ftruncate(fd, 1 << 16) < 0) {
        perror("ftruncate");
        goto error;
    }

    if (close(fd) < 0) {
        perror("close");
        goto error;
    }

    // open for testing
    fd = open(shm_path, open_flags);
    if (fd < 0) {
        perror("open");
        goto error;
    }

    shm = mmap(NULL, shm_size, mmap_prot, mmap_flags, fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap 2");
        goto error;
    }

    status = fi_fabric2(info, &fabric, 0, NULL);
    if (status < 0) {
        fi_perror(status, "open fabric");
        goto error;
    }

    status = fi_domain(fabric, info, &domain, NULL);
    if (status < 0) {
        fi_perror(status, "open domain");
        goto error;
    }

    // iovec
    mr_iov.iov_base = shm;
    mr_iov.iov_len = shm_size;

    // fi_mr_attr
    mr_attr.mr_iov = &mr_iov;
    mr_attr.iov_count = 1;
    mr_attr.access = FI_WRITE;
    mr_attr.offset = 0;
    mr_attr.requested_key = 0;
    mr_attr.context = NULL;
    mr_attr.auth_key_size = 0;
    mr_attr.auth_key = NULL;
    mr_attr.iface = FI_HMEM_SYSTEM;
    mr_attr.device.reserved = 0;
    mr_attr.hmem_data = NULL;
    mr_attr.page_size = 0;
    mr_attr.base_mr = NULL;
    mr_attr.sub_mr_cnt = 0;

    if ((status = fi_mr_regattr(domain, &mr_attr, 0, &mr)) < 0) {
        fi_perror(status, "fi_mr_regattr");
        goto error;
    }

    void *desc = fi_mr_desc(mr);
    if (desc == NULL) {
        fprintf(stderr, "NULL returned for mr desc");
        goto error;
    }

    printf("register success!\n");
    goto cleanup;

error:
    status = 1;

cleanup:
    if (mr != NULL) {
        if ((status = fi_close(&mr->fid)) < 0) {
            fprintf(stderr, "close fabric: %s", fi_strerror(status));
        }
    }

    if (domain != NULL) {
        if ((status = fi_close(&domain->fid)) < 0) {
            fprintf(stderr, "close domain: %s", fi_strerror(status));
        }
    }

    if (fabric != NULL) {
        if ((status = fi_close(&fabric->fid)) < 0) {
            fprintf(stderr, "close fabric: %s", fi_strerror(status));
        }
    }

    if (info != NULL)
        fi_freeinfo(info);
    if (node != NULL)
        free(node);
    if (service != NULL)
        free(service);

    return status;
}
