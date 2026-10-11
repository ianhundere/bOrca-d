#include "base.h"
#include "events_print.h"
#include "field.h"
#include "gbuffer.h"
#include "opstate.h"
#include "sim.h"
#include "vmio.h"
#include <ctype.h>
#include <errno.h>
#include <getopt.h>

static ORCA_NOINLINE void usage(void) { // clang-format off
fprintf(stderr,
"Usage: cli [options] infile\n\n"
"Options:\n"
"    -t <number>   Number of timesteps to simulate.\n"
"                  Must be 0 or a positive integer.\n"
"                  Default: 1\n"
"    -q or --quiet Don't print the grid to stdout.\n"
"    --events      After each tick, print that tick's output events, one\n"
"                  per line (NOTE, CC, CCI, PB, OSC, UDP, each prefixed\n"
"                  t<tick>), then 't<tick> GRID' and the grid. With -q,\n"
"                  print the event lines only.\n"
"    --seed <n>    Seed for the random operators. Default: 0\n"
"    --dialect <d> Operator dialect: borca (default) or upstream,\n"
"                  upstream Orca-c's operators: ! sends a plain CC,\n"
"                  ; UDP, = OSC, r is a banged R, and $ and & do\n"
"                  nothing.\n"
"    -h or --help  Print this message and exit.\n"
);} // clang-format on

enum { Argopt_events = UCHAR_MAX + 1, Argopt_seed, Argopt_dialect };

// Parses a non-negative decimal integer: digits only, no sign, no whitespace,
// no trailing text, no overflow, and at most max. Used by -t and --seed, so
// the golden suite's flags have one parser.
static bool parse_usz(char const *s, Usz max, Usz *out) {
  if (!isdigit((unsigned char)s[0]))
    return false;
  errno = 0;
  char *end = NULL;
  unsigned long long v = strtoull(s, &end, 10);
  if (*end != '\0' || errno == ERANGE || v > (unsigned long long)max)
    return false;
  *out = (Usz)v;
  return true;
}

int main(int argc, char **argv) {
  static struct option cli_options[] = {
      {"help", no_argument, 0, 'h'},
      {"quiet", no_argument, 0, 'q'},
      {"events", no_argument, 0, Argopt_events},
      {"seed", required_argument, 0, Argopt_seed},
      {"dialect", required_argument, 0, Argopt_dialect},
      {NULL, 0, NULL, 0}};

  char *input_file = NULL;
  Usz max_ticks = 1;
  bool print_output = true;
  bool print_events = false;
  Usz seed = 0;
  Orca_dialect dialect = Orca_dialect_borca;

  for (;;) {
    int c = getopt_long(argc, argv, "t:qh", cli_options, NULL);
    if (c == -1)
      break;
    switch (c) {
    case 't':
      if (!parse_usz(optarg, SIZE_MAX, &max_ticks)) {
        fprintf(stderr,
                "Bad timestep argument %s.\n"
                "Must be 0 or a positive integer.\n",
                optarg);
        return 1;
      }
      break;
    case 'q':
      print_output = false;
      break;
    case Argopt_events:
      print_events = true;
      break;
    case Argopt_seed:
      if (!parse_usz(optarg, SIZE_MAX, &seed)) {
        fprintf(stderr,
                "Bad seed argument %s.\n"
                "Must be 0 or a positive integer.\n",
                optarg);
        return 1;
      }
      break;
    case Argopt_dialect:
      if (!orca_dialect_from_name(optarg, &dialect)) {
        fprintf(stderr, "Unknown dialect %s. Expected borca or upstream.\n",
                optarg);
        return 1;
      }
      break;
    case 'h':
      usage();
      return 0;
    case '?':
      usage();
      return 1;
    }
  }

  if (optind == argc - 1) {
    input_file = argv[optind];
  } else if (optind < argc - 1) {
    fprintf(stderr, "Expected only 1 file argument.\n");
    usage();
    return 1;
  }

  if (input_file == NULL) {
    fprintf(stderr, "No input file.\n");
    usage();
    return 1;
  }

  Field field;
  field_init(&field);
  Field_load_error fle = field_load_file(input_file, &field);
  if (fle != Field_load_error_ok) {
    field_deinit(&field);
    fprintf(stderr, "File load error: %s.\n", field_load_error_string(fle));
    return 1;
  }
  Mbuf_reusable mbuf_r;
  mbuf_reusable_init(&mbuf_r);
  mbuf_reusable_ensure_size(&mbuf_r, field.height, field.width);
  Oevent_list oevent_list;
  oevent_list_init(&oevent_list);
  // The per-cell state of &, ; and r, owned here for the whole run.
  Opstate_store opstate;
  opstate_init(&opstate);
  Orca_run_ctx const ctx = {.opstate = &opstate, .dialect = dialect};
  for (Usz i = 0; i < max_ticks; ++i) {
    mbuffer_clear(mbuf_r.buffer, field.height, field.width);
    oevent_list_clear(&oevent_list);
    orca_run(field.buffer, mbuf_r.buffer, field.height, field.width, i,
             &oevent_list, seed, &ctx);
    if (print_events) {
      events_print(stdout, i, oevent_list.buffer, oevent_list.count);
      if (print_output) {
        printf("t%zu GRID\n", i);
        field_fput(&field, stdout);
      }
    }
  }
  mbuf_reusable_deinit(&mbuf_r);
  oevent_list_deinit(&oevent_list);
  opstate_free(&opstate);
  // With --events every tick already printed its grid; print the final grid
  // only in the plain mode, as before.
  if (print_output && !print_events)
    field_fput(&field, stdout);
  field_deinit(&field);
  // The output is a test oracle, so a write failure must not exit 0.
  if (fflush(stdout) != 0 || ferror(stdout)) {
    perror("cli: stdout");
    return 1;
  }
  return 0;
}
