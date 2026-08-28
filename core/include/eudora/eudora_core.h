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
    EUDORA_STATE_REDISTRIBUTED = 4,
    EUDORA_STATE_UNSENDABLE = 5,
    EUDORA_STATE_SENDABLE = 6,
    EUDORA_STATE_QUEUED = 7,
    EUDORA_STATE_FORWARDED = 8,
    EUDORA_STATE_SENT = 9,
    EUDORA_STATE_UNSENT = 10,
    EUDORA_STATE_TIMED = 11,
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
    /* New fields are appended only, so older callers stay layout-stable. */
    int64_t arrival_unix; /* when the message arrived here, Unix epoch UTC */
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
/* Set the label color index (0-15; bits 14-17 of flags). */
int eudora_mailbox_set_label(eudora_mailbox *mb, int32_t index, int label);
/* Set the display priority (1-5, 3 = normal; scaled like Display2Prior). */
int eudora_mailbox_set_priority(eudora_mailbox *mb, int32_t index,
                                int display_priority);
/* Set the junk score (clamped to -128..127; JunkSetScore). */
int eudora_mailbox_set_spam_score(eudora_mailbox *mb, int32_t index, int score);
/* Replace the summary's subject (the stored message text is unchanged). */
int eudora_mailbox_set_subject(eudora_mailbox *mb, int32_t index,
                               const char *subject);
/* Index of the summary with the given serial number, or -1 (FindSumBySerialNum). */
int32_t eudora_mailbox_find_by_serial(const eudora_mailbox *mb,
                                      int32_t serial_num);
/* Append a complete RFC 822 message (any line-end convention) to the mbox
 * with a proper sendmail envelope, add its summary (state 0 keeps the
 * scanner's verdict), and return the new index, or -1.  Call
 * eudora_mailbox_save to persist the TOC. */
int32_t eudora_mailbox_append_message(eudora_mailbox *mb, const char *raw,
                                      size_t len, uint8_t state);
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

/* ---- MIME parts -------------------------------------------------------- */

typedef struct {
    const char *type;      /* lowercased; owned by the message handle */
    const char *subtype;
    const char *filename;  /* "" when the part is unnamed */
    int transfer_encoding; /* same coding as eudora_message_transfer_encoding */
    int32_t depth;         /* 0 = the whole-message body */
    int is_attachment;
    int64_t size;          /* encoded body span, bytes */
} eudora_part_info;

/* Leaf MIME parts of the message in document order (multipart containers
 * are walked, not listed).  A plain message has exactly one part. */
int32_t eudora_message_part_count(const eudora_message *msg);
int eudora_message_part_info(const eudora_message *msg, int32_t index,
                             eudora_part_info *out);
/* The part's body decoded per its transfer encoding: malloc'd, *out_len
 * bytes plus an uncounted trailing NUL (the data may itself contain NULs).
 * Free with eudora_string_free. */
char *eudora_message_part_decode(const eudora_message *msg, int32_t index,
                                 size_t *out_len);

/* A text part decoded AND converted from its charset to UTF-8 (for display);
 * non-text parts return their raw decoded bytes.  malloc'd; free with
 * eudora_string_free. */
char *eudora_message_part_text(const eudora_message *msg, int32_t index);

/* Parse an address list into a NULL-terminated malloc'd array. */
char **eudora_parse_addresses(const char *header_value);
void eudora_addresses_free(char **addresses);

/* ---- POP3 -------------------------------------------------------------- */

enum {
    EUDORA_TLS_NONE = 0,     /* plaintext */
    EUDORA_TLS_STARTTLS = 1, /* STLS/STARTTLS upgrade */
    EUDORA_TLS_IMMEDIATE = 2 /* TLS from connect (POP3S/SMTPS ports) */
};

/* Fetch new messages into the mailbox at mbox_path (appending, updating the
 * TOC, exactly as the POP engine appended to In).  Messages already fetched
 * on an earlier check are recognized by their UIDL hash (the legacy
 * leave-mail-on-server bookkeeping) and skipped; servers without UIDL fall
 * back to fetching everything.  delete_from_server: DELE after retrieval
 * (and for already-fetched messages, without re-downloading them).
 * Returns the number of messages fetched, or -1. */
int32_t eudora_pop3_fetch(const char *host, uint16_t port, int tls_mode,
                          const char *user, const char *password,
                          const char *mbox_path, int delete_from_server);

/* Progress/cancel callback for eudora_pop3_fetch_ex.  stage is one of
 * "connect", "auth", "list", "retr"; for "retr", done is the number of
 * messages stored so far and total the number that will be fetched (both 0
 * for the other stages).  Return nonzero to cancel: messages already stored
 * are kept and the TOC is still written. */
typedef int (*eudora_progress_fn)(void *ctx, const char *stage,
                                  int32_t done, int32_t total);

/* eudora_pop3_fetch with progress reporting and cancellation.  progress may
 * be NULL; ctx is passed through to it.  A cancelled fetch returns the
 * number of messages stored before the cancel (never -1 for the cancel
 * itself). */
int32_t eudora_pop3_fetch_ex(const char *host, uint16_t port, int tls_mode,
                             const char *user, const char *password,
                             const char *mbox_path, int delete_from_server,
                             eudora_progress_fn progress, void *ctx);

/* Checking-mail options beyond delete-everything (the classic Checking Mail
 * panel).  Zero-initialize for the plain behavior. */
typedef struct {
    /* DELE every message after retrieval. */
    int delete_from_server;
    /* When leaving mail on the server: also DELE already-fetched messages
     * whose local arrival is older than this many days (PREF_LMOS_XDAYS;
     * 0 = keep forever).  Ignored when delete_from_server is set. */
    int32_t leave_on_server_days;
    /* Skip (leave unfetched on the server) messages larger than this many
     * KB (PREF_NO_BIGGIES / big-message limit; 0 = no limit).  Skipped
     * messages are reconsidered on every later check. */
    int32_t max_message_k;
} eudora_pop3_options;

/* eudora_pop3_fetch_ex with the full option set; options may be NULL. */
int32_t eudora_pop3_fetch_opts(const char *host, uint16_t port, int tls_mode,
                               const char *user, const char *password,
                               const char *mbox_path,
                               const eudora_pop3_options *options,
                               eudora_progress_fn progress, void *ctx);

/* ---- IMAP -------------------------------------------------------------- */

/* Fetch new messages from an IMAP mailbox (imap_mailbox NULL/"" = INBOX)
 * into the local mailbox at mbox_path.  Messages already fetched are
 * recognized by the hash of "UIDVALIDITY/UID" stored in their summaries —
 * the IMAP analog of the POP UIDL bookkeeping (a UIDVALIDITY change
 * naturally re-fetches, per RFC 3501).  Server flags choose the initial
 * state (\Seen -> read, \Answered -> replied).  delete_from_server flags
 * fetched (and previously fetched) messages \Deleted and expunges.
 * Progress stages and cancellation exactly as eudora_pop3_fetch_ex. */
int32_t eudora_imap_fetch_ex(const char *host, uint16_t port, int tls_mode,
                             const char *user, const char *password,
                             const char *imap_mailbox,
                             const char *mbox_path, int delete_from_server,
                             eudora_progress_fn progress, void *ctx);

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
eudora_filters *eudora_filters_new(void);
void eudora_filters_free(eudora_filters *f);
int32_t eudora_filters_count(const eudora_filters *f);
int eudora_filters_save(const eudora_filters *f, const char *path);

/* One filter record, for the editor UI.  Strings returned by _get are owned
 * by the set and valid until it is mutated or freed.  verb strings use the
 * on-disk forms ("contains", "!is", "regex", ...); conjunction is one of
 * "ignore", "and", "or", "unless". */
typedef struct {
    const char *name;
    int32_t id;
    int incoming, outgoing, manual;
    const char *header1, *verb1, *value1;
    const char *conjunction;
    const char *header2, *verb2, *value2;
} eudora_filter_info;

int eudora_filters_get(const eudora_filters *f, int32_t index,
                       eudora_filter_info *out);
/* NULL string fields keep their current values. */
int eudora_filters_set(eudora_filters *f, int32_t index,
                       const eudora_filter_info *in);
/* Appends a blank incoming filter; returns its index. */
int32_t eudora_filters_add(eudora_filters *f, const char *name);
int eudora_filters_remove(eudora_filters *f, int32_t index);
int eudora_filters_move(eudora_filters *f, int32_t from, int32_t to);

int32_t eudora_filter_action_count(const eudora_filters *f, int32_t index);
int eudora_filter_action_get(const eudora_filters *f, int32_t index,
                             int32_t action, const char **keyword,
                             const char **value);
int eudora_filter_action_add(eudora_filters *f, int32_t index,
                             const char *keyword, const char *value);
int eudora_filter_action_set(eudora_filters *f, int32_t index, int32_t action,
                             const char *keyword, const char *value);
int eudora_filter_action_remove(eudora_filters *f, int32_t index,
                                int32_t action);

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
