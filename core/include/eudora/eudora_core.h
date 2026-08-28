/* EudoraCore — C bridging interface for Swift / Objective-C frontends.
 *
 * A flat, extern "C" surface over the C++ core so a SwiftUI application can
 * import this single header (via a module map or bridging header) and drive
 * the mail engine: open mailboxes, list and read messages, fetch via POP3,
 * send via SMTP, and evaluate filters.
 *
 * Conventions:
 *   - All strings are UTF-8, NUL-terminated.
 *   - Functions returning char* transfer ownership; free with
 *     eudora_string_free.
 *   - Opaque handles are freed with their matching *_free/_close call.
 *   - Functions returning int use 1 for success, 0 for failure unless
 *     documented otherwise; eudora_last_error() describes the most recent
 *     failure on the calling thread.
 */

#ifndef EUDORA_CORE_H
#define EUDORA_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- library ---------------------------------------------------------- */

const char *eudora_core_version(void);
/* Thread-local description of the last failure ("" if none). */
const char *eudora_last_error(void);
void eudora_string_free(char *s);

/* ---- mailboxes -------------------------------------------------------- */

typedef struct eudora_mailbox eudora_mailbox;

/* Message state (StateEnum). */
enum {
    EUDORA_STATE_UNREAD = 1,
    EUDORA_STATE_READ = 2,
    EUDORA_STATE_REPLIED = 3,
    EUDORA_STATE_FORWARDED = 8,
    EUDORA_STATE_SENT = 9,
    EUDORA_STATE_SENDABLE = 6,
    EUDORA_STATE_QUEUED = 7,
    EUDORA_STATE_REBUILT = 14
};

typedef struct {
    int32_t index;
    int32_t offset;
    int32_t length;
    int32_t body_offset;
    int32_t serial_num;
    int64_t date_unix;      /* Date: header as Unix epoch UTC */
    int32_t orig_zone_minutes;
    uint32_t flags;         /* FLAG_* bits */
    uint32_t opts;          /* OPT_* bits */
    uint8_t state;          /* EUDORA_STATE_* */
    int8_t spam_score;
    uint8_t priority_display; /* 1..5 (3 = normal) */
    uint32_t uid_hash;
    uint32_t msg_id_hash;
    const char *from;    /* valid until the mailbox handle is freed/reloaded */
    const char *subject;
} eudora_summary;

/* Open a mailbox: reads "<path>.toc" when present and valid, else rebuilds
 * it by scanning the mbox file (CheckTOC semantics). */
eudora_mailbox *eudora_mailbox_open(const char *mbox_path);
void eudora_mailbox_close(eudora_mailbox *mb);

int32_t eudora_mailbox_count(const eudora_mailbox *mb);
/* Fills `out`; returns 1 on success. String pointers remain owned by mb. */
int eudora_mailbox_summary(const eudora_mailbox *mb, int32_t index,
                           eudora_summary *out);

/* Raw message text (headers+body) as stored in the mbox. */
char *eudora_mailbox_read_message(const eudora_mailbox *mb, int32_t index);

/* Set state / mark for later persistence. */
int eudora_mailbox_set_state(eudora_mailbox *mb, int32_t index, uint8_t state);
/* Remove the message's summary (the bytes remain until compaction). */
int eudora_mailbox_delete(eudora_mailbox *mb, int32_t index);
/* Rewrite the mbox dropping unreferenced bytes and save the TOC. */
int eudora_mailbox_compact(eudora_mailbox *mb);
/* Persist the .toc file (WriteTOC). */
int eudora_mailbox_save(eudora_mailbox *mb);

/* ---- message parsing --------------------------------------------------- */

typedef struct eudora_message eudora_message;

eudora_message *eudora_message_parse(const char *raw, size_t len);
void eudora_message_free(eudora_message *msg);

/* Unfolded raw header value, or NULL if absent. */
char *eudora_message_header(const eudora_message *msg, const char *name);
/* RFC 2047-decoded header value ("" if absent). */
char *eudora_message_header_decoded(const eudora_message *msg, const char *name);
const char *eudora_message_body(const eudora_message *msg);
const char *eudora_message_content_type(const eudora_message *msg);
const char *eudora_message_content_subtype(const eudora_message *msg);
const char *eudora_message_boundary(const eudora_message *msg);
const char *eudora_message_filename(const eudora_message *msg);
/* 0 none/7bit/8bit/binary, 1 quoted-printable, 2 base64, 3 other */
int eudora_message_transfer_encoding(const eudora_message *msg);

/* Decode a base64 or quoted-printable body part; returns malloc'd bytes and
 * sets *out_len.  encoding: 1 = QP, 2 = base64. */
char *eudora_decode_body(const char *data, size_t len, int encoding,
                         size_t *out_len);

/* Parse an address list into a NULL-terminated malloc'd array. */
char **eudora_parse_addresses(const char *header_value);
void eudora_addresses_free(char **addresses);

/* ---- POP3 -------------------------------------------------------------- */

enum {
    EUDORA_TLS_NONE = 0,     /* plaintext */
    EUDORA_TLS_STARTTLS = 1, /* STLS/STARTTLS upgrade */
    EUDORA_TLS_IMMEDIATE = 2 /* TLS from connect (POP3S/SMTPS ports) */
};

/* Fetch all messages into the mailbox at mbox_path (appending, updating the
 * TOC, exactly as the POP engine appended to In).  delete_from_server: DELE
 * after retrieval.  Returns the number of messages fetched, or -1. */
int32_t eudora_pop3_fetch(const char *host, uint16_t port, int tls_mode,
                          const char *user, const char *password,
                          const char *mbox_path, int delete_from_server);

/* ---- SMTP -------------------------------------------------------------- */

/* Send a fully formed RFC 822 message (CR, LF, or CRLF line ends).
 * recipients: comma-separated address list.  user may be NULL/"" for
 * unauthenticated submission.  Returns the final SMTP code (2xx=success). */
int eudora_smtp_send(const char *host, uint16_t port, int tls_mode,
                     const char *user, const char *password,
                     const char *from, const char *recipients,
                     const char *message, size_t message_len);

/* ---- message composition ----------------------------------------------- */

typedef struct eudora_composer eudora_composer;

eudora_composer *eudora_composer_new(void);
void eudora_composer_free(eudora_composer *c);

void eudora_composer_from(eudora_composer *c, const char *name,
                          const char *address);
void eudora_composer_to(eudora_composer *c, const char *address_list);
void eudora_composer_cc(eudora_composer *c, const char *address_list);
void eudora_composer_bcc(eudora_composer *c, const char *address_list);
void eudora_composer_reply_to(eudora_composer *c, const char *address);
void eudora_composer_subject(eudora_composer *c, const char *utf8_subject);
void eudora_composer_body(eudora_composer *c, const char *utf8_body);
void eudora_composer_header(eudora_composer *c, const char *name,
                            const char *value);
void eudora_composer_priority(eudora_composer *c, int display_priority);
/* content_type/filename may be NULL to guess/use the path's name. */
void eudora_composer_attach(eudora_composer *c, const char *path,
                            const char *content_type, const char *filename);

/* The full RFC 822 message (CRLF), ready for eudora_smtp_send; NULL if an
 * attachment can't be read.  Free with eudora_string_free. */
char *eudora_composer_build(const eudora_composer *c);
/* Envelope pieces for eudora_smtp_send. */
char *eudora_composer_sender(const eudora_composer *c);
/* Comma-joined bare recipient addresses (to+cc+bcc). */
char *eudora_composer_recipients(const eudora_composer *c);

/* ---- address book (Eudora Nicknames) ----------------------------------- */

typedef struct eudora_addressbook eudora_addressbook;

eudora_addressbook *eudora_addressbook_load(const char *path);
eudora_addressbook *eudora_addressbook_parse(const char *text);
void eudora_addressbook_free(eudora_addressbook *ab);
int eudora_addressbook_save(const eudora_addressbook *ab, const char *path);

int32_t eudora_addressbook_count(const eudora_addressbook *ab);
/* Owned by the book; valid until it is mutated or freed. */
const char *eudora_addressbook_name(const eudora_addressbook *ab, int32_t i);
const char *eudora_addressbook_addresses(const eudora_addressbook *ab, int32_t i);
const char *eudora_addressbook_notes(const eudora_addressbook *ab, int32_t i);

/* Add or replace a nickname. */
int eudora_addressbook_set(eudora_addressbook *ab, const char *name,
                           const char *addresses, const char *notes);
int eudora_addressbook_remove(eudora_addressbook *ab, const char *name);

/* Recursively expand nicknames in an address list; NULL-terminated array,
 * free with eudora_addresses_free. */
char **eudora_addressbook_expand(const eudora_addressbook *ab,
                                 const char *address_list);
/* Does the address appear in any nickname (filters' intersectsFile)? */
int eudora_addressbook_contains(const eudora_addressbook *ab,
                                const char *address);

/* ---- filters ----------------------------------------------------------- */

typedef struct eudora_filters eudora_filters;

typedef struct {
    const char *filter_name;
    const char *keyword; /* e.g. "transfer", "junk", "stop" */
    const char *value;
} eudora_fired_action;

enum {
    EUDORA_FILTER_INCOMING = 0,
    EUDORA_FILTER_OUTGOING = 1,
    EUDORA_FILTER_MANUAL = 2
};

eudora_filters *eudora_filters_load(const char *path);
eudora_filters *eudora_filters_parse(const char *text);
void eudora_filters_free(eudora_filters *f);
int32_t eudora_filters_count(const eudora_filters *f);
int eudora_filters_save(const eudora_filters *f, const char *path);

/* Evaluate against a raw message; returns a malloc'd array of fired actions
 * (in execution order) and sets *out_count.  Free with
 * eudora_fired_actions_free.  Strings remain owned by `f` or the array. */
eudora_fired_action *eudora_filters_run(const eudora_filters *f, int event,
                                        const char *raw_message, size_t len,
                                        int32_t *out_count);
/* Like eudora_filters_run, with an address book backing the
 * intersects-file verb (may be NULL). */
eudora_fired_action *eudora_filters_run_with_book(
    const eudora_filters *f, int event, const char *raw_message, size_t len,
    const eudora_addressbook *book, int32_t *out_count);
void eudora_fired_actions_free(eudora_fired_action *actions, int32_t count);

#ifdef __cplusplus
}
#endif

#endif /* EUDORA_CORE_H */
