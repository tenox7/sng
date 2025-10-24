#include "snmp_client.h"
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>

#define ASN_SEQUENCE 0x30
#define ASN_INTEGER 0x02
#define ASN_OCTET_STRING 0x04
#define ASN_NULL 0x05
#define ASN_OID 0x06
#define ASN_COUNTER32 0x41
#define SNMP_GET 0xA0

static int encode_length(unsigned char *buf, int len) {
    if (len < 128) {
        buf[0] = len;
        return 1;
    }
    if (len < 256) {
        buf[0] = 0x81;
        buf[1] = len;
        return 2;
    }
    buf[0] = 0x82;
    buf[1] = (len >> 8) & 0xFF;
    buf[2] = len & 0xFF;
    return 3;
}

static int encode_integer(unsigned char *buf, uint32_t val) {
    unsigned char *p;
    int len;
    int i;
    int hlen;
    uint32_t temp;
    unsigned char t;

    p = buf + 2;
    len = 0;

    if (val == 0) {
        p[0] = 0;
        len = 1;
    } else {
        temp = val;
        while (temp > 0) {
            p[len] = temp & 0xFF;
            temp >>= 8;
            len++;
        }

        for (i = 0; i < len / 2; i++) {
            t = p[i];
            p[i] = p[len - 1 - i];
            p[len - 1 - i] = t;
        }

        if (p[0] & 0x80) {
            for (i = len; i > 0; i--) {
                p[i] = p[i - 1];
            }
            p[0] = 0;
            len++;
        }
    }

    buf[0] = ASN_INTEGER;
    hlen = encode_length(buf + 1, len);
    if (hlen > 1) {
        for (i = len - 1; i >= 0; i--) {
            p[hlen - 1 + i] = p[i];
        }
    }
    return 1 + hlen + len;
}

static int encode_string(unsigned char *buf, const char *str) {
    int len;
    int hlen;

    len = strlen(str);
    buf[0] = ASN_OCTET_STRING;
    hlen = encode_length(buf + 1, len);
    memcpy(buf + 1 + hlen, str, len);
    return 1 + hlen + len;
}

static int encode_oid(unsigned char *buf, const uint32_t *oid, int oid_len) {
    unsigned char *p;
    int i;
    int len;
    int hlen;

    p = buf + 2;
    len = 0;

    p[len++] = oid[0] * 40 + oid[1];

    for (i = 2; i < oid_len; i++) {
        uint32_t val;
        int j;
        unsigned char tmp[5];
        int tlen;

        val = oid[i];
        tlen = 0;

        if (val == 0) {
            p[len++] = 0;
        } else {
            while (val > 0) {
                tmp[tlen++] = (val & 0x7F) | 0x80;
                val >>= 7;
            }
            tmp[0] &= 0x7F;

            for (j = tlen - 1; j >= 0; j--) {
                p[len++] = tmp[j];
            }
        }
    }

    buf[0] = ASN_OID;
    hlen = encode_length(buf + 1, len);
    if (hlen > 1) {
        for (i = len - 1; i >= 0; i--) {
            p[hlen - 1 + i] = p[i];
        }
    }
    return 1 + hlen + len;
}

static int encode_null(unsigned char *buf) {
    buf[0] = ASN_NULL;
    buf[1] = 0;
    return 2;
}

static int decode_length(const unsigned char *buf, int *len_size) {
    if (buf[0] < 128) {
        *len_size = 1;
        return buf[0];
    }
    if (buf[0] == 0x81) {
        *len_size = 2;
        return buf[1];
    }
    if (buf[0] == 0x82) {
        *len_size = 3;
        return (buf[1] << 8) | buf[2];
    }
    *len_size = 1;
    return 0;
}

static int decode_counter32(const unsigned char *buf, int len, uint32_t *result) {
    uint32_t val;
    int i;

    val = 0;
    for (i = 0; i < len; i++) {
        val = (val << 8) | buf[i];
    }
    *result = val;
    return 1;
}

static int build_get_request(unsigned char *buf, const char *community,
                              const uint32_t *oid, int oid_len, uint32_t req_id) {
    unsigned char varbind[128];
    unsigned char varbind_seq[256];
    unsigned char varbind_list[256];
    unsigned char pdu_data[512];
    unsigned char pdu[512];
    unsigned char message[1024];
    int pos, len, hlen;

    pos = 0;
    pos += encode_oid(varbind + pos, oid, oid_len);
    pos += encode_null(varbind + pos);
    len = pos;

    varbind_seq[0] = ASN_SEQUENCE;
    hlen = encode_length(varbind_seq + 1, len);
    memcpy(varbind_seq + 1 + hlen, varbind, len);
    len = 1 + hlen + len;

    varbind_list[0] = ASN_SEQUENCE;
    hlen = encode_length(varbind_list + 1, len);
    memcpy(varbind_list + 1 + hlen, varbind_seq, len);
    len = 1 + hlen + len;

    pos = 0;
    pos += encode_integer(pdu_data + pos, req_id);
    pos += encode_integer(pdu_data + pos, 0);
    pos += encode_integer(pdu_data + pos, 0);
    memcpy(pdu_data + pos, varbind_list, len);
    pos += len;
    len = pos;

    pdu[0] = SNMP_GET;
    hlen = encode_length(pdu + 1, len);
    memcpy(pdu + 1 + hlen, pdu_data, len);
    len = 1 + hlen + len;

    pos = 0;
    pos += encode_integer(message + pos, 0);
    pos += encode_string(message + pos, community);
    memcpy(message + pos, pdu, len);
    pos += len;
    len = pos;

    buf[0] = ASN_SEQUENCE;
    hlen = encode_length(buf + 1, len);
    memcpy(buf + 1 + hlen, message, len);
    return 1 + hlen + len;
}

static int parse_get_response(const unsigned char *buf, int buf_len, uint32_t *result) {
    const unsigned char *p;
    int len;
    int hlen;
    int i;
    int val_len;

    p = buf;

    if (p[0] != ASN_SEQUENCE) return 0;
    p++;
    len = decode_length(p, &hlen);
    p += hlen;

    if (p[0] != ASN_INTEGER) return 0;
    p++;
    len = decode_length(p, &hlen);
    p += hlen + len;

    if (p[0] != ASN_OCTET_STRING) return 0;
    p++;
    len = decode_length(p, &hlen);
    p += hlen + len;

    if (p[0] != 0xA2) return 0;
    p++;
    len = decode_length(p, &hlen);
    p += hlen;

    if (p[0] != ASN_INTEGER) return 0;
    p++;
    len = decode_length(p, &hlen);
    p += hlen + len;

    if (p[0] != ASN_INTEGER) return 0;
    p++;
    len = decode_length(p, &hlen);
    p += hlen + len;

    if (p[0] != ASN_INTEGER) return 0;
    p++;
    len = decode_length(p, &hlen);
    p += hlen + len;

    if (p[0] != ASN_SEQUENCE) return 0;
    p++;
    len = decode_length(p, &hlen);
    p += hlen;

    if (p[0] != ASN_SEQUENCE) return 0;
    p++;
    len = decode_length(p, &hlen);
    p += hlen;

    if (p[0] != ASN_OID) return 0;
    p++;
    len = decode_length(p, &hlen);
    p += hlen + len;

    if (p[0] != ASN_COUNTER32) return 0;
    p++;
    val_len = decode_length(p, &hlen);
    p += hlen;

    return decode_counter32(p, val_len, result);
}

int snmp_get_counter32(const char *host, const char *community,
                       const uint32_t *oid, int oid_len, uint32_t *result) {
    int sock;
    struct sockaddr_in addr;
    unsigned char req_buf[SNMP_MAX_MSG_SIZE];
    unsigned char resp_buf[SNMP_MAX_MSG_SIZE];
    int req_len;
    int resp_len;
    struct hostent *he;
    uint32_t req_id;
    struct timeval tv;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return 0;

    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    he = gethostbyname(host);
    if (!he) {
        close(sock);
        return 0;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SNMP_PORT);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);

    req_id = (uint32_t)time(NULL);
    req_len = build_get_request(req_buf, community, oid, oid_len, req_id);

    if (sendto(sock, req_buf, req_len, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return 0;
    }

    resp_len = recvfrom(sock, resp_buf, SNMP_MAX_MSG_SIZE, 0, NULL, NULL);
    close(sock);

    if (resp_len <= 0) return 0;

    return parse_get_response(resp_buf, resp_len, result);
}
