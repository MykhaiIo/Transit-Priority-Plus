/* Signal_receiver.ino file created by Mykhailo Lytvynenko
 *
 * Created:   Wed Mar 10 2020
 * Processor: Arduino Uno
 *
 * Active buttons level is LOW
 *
 * Modes of operation that enables specified directions are following:
 * individual:
 *          Phase 1 - { > }
 *          Phase 2 - { ^ }
 *          Phase 3 - { < }
 *
 * left_forward_and_right:
 *          Phase 1 - { <, ^ }
 *          Phase 2 - { > }
 *
 * forward_right_and_left:
 *          Phase 1 - { ^, > }
 *          Phase 2 - { < }
 *
 * left_right_and_forward:
 *          Phase 1 - { <, > }
 *          Phase 2 - { ^ }
 *
 * universal:
 *          Phase - { <, ^, > }
 * This mode is completely timed and does not react to transit except phase
 * extension condition checking.
 *
 * Extension of phase allows for transit to pass intrsection just after previous
 * transit if needed direction is subset of current direction(s) e.g. { <, ^}
 * can be extended to { < } as well as to { ^ }  but not to { > }. Similary
 * { > } can be extended to { > } only.
 *
 *
 * States S2 and S3 are inside so-called hyperstate and are used in all modes
 * except "universal". The purpose is to indicate that transit signal controller
 * is going to enable necessary phase for first waiting transit in the queue.
 *
 * Transit is supposed to leave intersecrion after enabling necessary phase
 */

#include "Signal_receiver.h"

#define rx 2
#define tx 3

#define START_BTN 12

typedef String Line;
SoftwareSerial BTserial(rx,tx);

// Timers to leave state after specified time
unsigned long state_timer = 0;
unsigned long hyperstate_timer = 0;

unsigned long finish_at = 0;

// Sets of used lines (from enum Lines) for the specific intersection
// Lines are splitted by directions where transit need to go
const std::vector<Line> left_lines{
    make_line(36, Terminuses::Condat_Versanas, Terminuses::Pl_W_Churchill),
    make_line(44, Terminuses::Solignac_Bourg, Terminuses::Pl_W_Churchill)
};
const std::vector<Line> forward_lines{
    make_line(2, Terminuses::P_Curie, Terminuses::Pole_La_Bastide),
    make_line(4, Terminuses::Pole_St_Lazare, Terminuses::Montjovis),
    make_line(24, Terminuses::Fontgeaudrant, Terminuses::Pl_W_Churchill)
    };

const std::vector<Line> right_lines{};

// Queue to store waiting transit
DataQueue<Line> q_lines(10);

typedef struct {
  uint8_t mode : 3;

  boolean phase_extension_left : 1;    // 1 enables left extension (XL on state diagram)

  boolean phase_extension_forward : 1; // 1 enables forward extension (XF on state diagram)

  boolean phase_extension_right : 1;   // 1 enables right extended (XR on state diagram)

  boolean deviations_handled : 1;
} input_config;

boolean Start = 0;

input_config transit_signal_config = {
    left_right_and_forward, 1, 0, 1, 0 // defines operation mode of transit signal controller
    };

State state = S0_IDLE;
Line old_transit_line = ""; // previous received line from BTserial (with vehicle num)
Line detected_transit_line = ""; // new received line from BTserial (with vehicle num)
Line current_transit_line = ""; // first transit line from the queue (without vehicle num)

void setup() {
  pinMode(LEFT_LED_PIN, OUTPUT);
  pinMode(MIDDLE_LED_PIN, OUTPUT);
  pinMode(RIGHT_LED_PIN, OUTPUT);
  pinMode(BOTTOM_LED_PIN, OUTPUT);
  pinMode(AUX_LED_PIN, OUTPUT);

  pinMode(rx, INPUT);
  pinMode(tx, OUTPUT);
  Serial.begin(9600);
    Serial.println("Arduino with HC-05 is ready");
 
    // start communication with the HC-05 using 9600
    BTserial.begin(9600);  
    Serial.println("BTserial started at 9600");

  state_timer = millis(); // initial state_timer initialization
}

Line make_line(const uint16_t num,
                 const Terminuses provenance,
                 const Terminuses destination,
                 std::initializer_list<Deviations> deviations = {}) {
  // only involved signals will handle deviations transmitted
  // after certain sequence of defined values, others will ignore it
  Line line = String(num, DEC) + "<<" + String((uint8_t)provenance, HEX) + ">>" + String((uint8_t)destination, HEX) + "^^";
  const Deviations *p_deviations = deviations.begin();
  while (p_deviations != deviations.end())
  {
    line += String((uint8_t)*p_deviations, HEX) + '-';
    ++p_deviations;
  }
  return line + "#\n";
}

void read_transit_line() {
  if (!q_lines.isEmpty()) {
    current_transit_line = q_lines.front(); // get first wating transit in queue
  } else {
    current_transit_line = "";
  }  
}

void add_line_to_queue() {
  String number, provenance, destination, deviations;
  String data = "", line_to_enqueue = "";
  if (BTserial.available() > 0) {
    char buff = BTserial.read();
    data.concat(buff);
    if (buff == '}') {
      detected_transit_line = data;
      data = "";
    }

    // proceed to new line analysis if its different vehicle detected
    if (detected_transit_line != old_transit_line) {
      // check validity of detected line data
      if ((detected_transit_line.indexOf("<<") - detected_transit_line.indexOf("{{") + 2) > 2) {
        number = detected_transit_line.substring(detected_transit_line.indexOf("{{") + 2, detected_transit_line.indexOf("<<"));
        provenance = detected_transit_line.substring(detected_transit_line.indexOf("<<") + 2, detected_transit_line.indexOf(">>"));
        destination = detected_transit_line.substring(detected_transit_line.indexOf(">>") + 2, detected_transit_line.indexOf("^^"));
        deviations = detected_transit_line.substring(detected_transit_line.indexOf("^^") + 2, detected_transit_line.indexOf('#'));
        line_to_enqueue = number + "<<" + provenance + ">>" + destination + "^^";
        if (transit_signal_config.deviations_handled) {
          line_to_enqueue.concat(deviations + "#\n");
        } else {
          line_to_enqueue.concat("#\n");
        }

        q_lines.enqueue(line_to_enqueue);
        Serial.print(millis() / 1000);
        Serial.print(" s -> ");
        Serial.print("L");
        Serial.print(line_to_enqueue);
        Serial.println(" PUSHED");
        Serial.print("Lines in queue: ");
        Serial.println(q_lines.item_count());
        Serial.print("Current state is: S");
        Serial.println(state);
        Serial.println(" ");
        old_transit_line = detected_transit_line;
      }
    }
  }
}

boolean check_line_to_serve(Line line,
                            const std::vector<Line> &lines_set) {
  for (size_t line_no = 0; line_no < lines_set.size(); ++line_no) {
    if (line == lines_set[line_no]) {
      return 1;
    }
  }
  return 0;
}

void serve_transit_line(const Line line) {
  if (!q_lines.isEmpty()) {
    if (line == q_lines.front()) { 
      current_transit_line = q_lines.dequeue();
      Serial.print(millis() / 1000);
      Serial.print(" s -> ");
      Serial.print("L");
      Serial.print(line);
      Serial.println(" SERVED");
      Serial.print("Lines in queue: ");
      Serial.println(q_lines.item_count());
      if (!q_lines.isEmpty()) {
        Serial.print("Waiting is L");
        Serial.println(q_lines.front());
      }
      Serial.print("Current state is: S");
      Serial.println(state);
      Serial.println(" ");
    }
  }
}

void timed_automaton_run(const Line line) {
  switch (state) {
  case S0_IDLE:
    // after entering in idle state universal stored lines leave computer system
    while (!q_lines.isEmpty()) {
      q_lines.dequeue();
    }

    if (!Start) {
      state = S0_IDLE;
    } else if (transit_signal_config.mode == universal) {
      state = S14_INHIBIT_ALL_MODE;
      Serial.println("Current mode is { <, ^, > }");

      transit_signal_config.phase_extension_left
          ? Serial.println("Left phase extension is on")
          : Serial.println("Left phase extension is off");
      transit_signal_config.phase_extension_forward
          ? Serial.println("Forward phase extension is on")
          : Serial.println("Forward phase extension is off");
      transit_signal_config.phase_extension_right
          ? Serial.println("Right phase extension is on")
          : Serial.println("Right phase extension is off");
      transit_signal_config.deviations_handled
          ? Serial.println("Deviations are handled")
          : Serial.println("Deviations are not handled");

      Serial.println(" ");
    } else {
      state = S1_WAIT;
      switch (transit_signal_config.mode) {
      case individual:
        Serial.println("Current mode is { < } { ^ } { > }");
        break;
      case left_forward_and_right:
        Serial.println("Current mode is { <, ^ } { > }");
        break;
      case forward_right_and_left:
        Serial.println("Current mode is { ^, > } { < }");
        break;
      case left_right_and_forward:
        Serial.println("Current mode is { <, > } { ^ }");
        break;
      case universal:
        Serial.println("Current mode is { <, ^, > }");
        break;
      }

      transit_signal_config.phase_extension_left
          ? Serial.println("Left phase extension is on")
          : Serial.println("Left phase extension is off");
      transit_signal_config.phase_extension_forward
          ? Serial.println("Forward phase extension is on")
          : Serial.println("Forward phase extension is off");
      transit_signal_config.phase_extension_right
          ? Serial.println("Right phase extension is on")
          : Serial.println("Right phase extension is off");
      transit_signal_config.deviations_handled
          ? Serial.println("Deviations are handled")
          : Serial.println("Deviations are not handled");

      Serial.println(" ");
    }
    break;

  case S1_WAIT:
    if (!Start) {
      state = S0_IDLE;
    } else if (check_line_to_serve(line, left_lines) |
               check_line_to_serve(line, forward_lines) |
               check_line_to_serve(line, right_lines)) {
      state = S2_BLINKING_ON;
      finish_at =
          millis() + blinking_duration; // define moment of stopping blinking
      hyperstate_timer = millis();
    } else {
      state = S1_WAIT;
    }
    state_timer = millis(); // re-initialize state_timer for next state
    break;

  case S2_BLINKING_ON:
    if (millis() < finish_at) {
      unsigned long currentMillis = millis();
      if (currentMillis - hyperstate_timer >= t_blink_half_period) {
        hyperstate_timer = currentMillis;
        state = S3_BLINKING_OFF;
      }
    }

    // cheking for ellapsed time after entering state (non-blocking delay)
    if (millis() - state_timer >= blinking_duration) {
      do_at_leaving_hyperstate(line);
    }
    break;

  case S3_BLINKING_OFF:
    if (millis() < finish_at) {
      unsigned long currentMillis = millis();
      if (currentMillis - hyperstate_timer >= t_blink_half_period) {
        hyperstate_timer = currentMillis;
        state = S2_BLINKING_ON;
      }
    }

    if (millis() - state_timer >= blinking_duration) {
      do_at_leaving_hyperstate(line);
    }
    break;

  case S4_LEFT:
    if (millis() - state_timer >= t_turn_phase) {
      if (check_line_to_serve(line, left_lines) & transit_signal_config.phase_extension_left) {
        state = S5_EXTENDED_LEFT;
        serve_transit_line(line);
      } else {
        state = S13_INHIBIT;
      }
      state_timer = millis();
    }
    break;

  case S5_EXTENDED_LEFT:
    if (millis() - state_timer >= t_extended_turn_phase) {
      if (transit_signal_config.mode == universal) {
        state = S14_INHIBIT_ALL_MODE;
      } else {
        state = S13_INHIBIT;
      }
      state_timer = millis();
    }
    break;

  case S6_LEFT_FORWARD:
    if (millis() - state_timer >= t_turn_phase) {
      if (check_line_to_serve(line, left_lines) & transit_signal_config.phase_extension_left) {
        state = S5_EXTENDED_LEFT;
        serve_transit_line(line);
      } else if (check_line_to_serve(line, forward_lines) &
                 transit_signal_config.phase_extension_forward) {
        state = S8_EXTENDED_FORWARD;
        serve_transit_line(line);
      } else {
        state = S13_INHIBIT;
      }
      state_timer = millis();
    }
    break;

  case S7_FORWARD:
    if (millis() - state_timer >= t_forward_phase) {
      if (check_line_to_serve(line, forward_lines) &
          transit_signal_config.phase_extension_forward) {
        state = S8_EXTENDED_FORWARD;
        serve_transit_line(line);
      } else {
        state = S13_INHIBIT;
      }
      state_timer = millis();
    }
    break;

  case S8_EXTENDED_FORWARD:
    if (millis() - state_timer >= t_extended_forward_phase) {
      if (transit_signal_config.mode == universal) {
        state = S14_INHIBIT_ALL_MODE;
      } else {
        state = S13_INHIBIT;
      }
      state_timer = millis();
    }
    break;

  case S9_FORWARD_RIGHT:
    if (millis() - state_timer >= t_turn_phase) {
      if (check_line_to_serve(line, forward_lines) &
          transit_signal_config.phase_extension_forward) {
        state = S8_EXTENDED_FORWARD;
        serve_transit_line(line);
      } else if (check_line_to_serve(line, right_lines) &
                 transit_signal_config.phase_extension_right) {
        state = S11_EXTENDED_RIGHT;
        serve_transit_line(line);
      } else {
        state = S13_INHIBIT;
      }
      state_timer = millis();
    }
    break;

  case S10_RIGHT:
    if (millis() - state_timer >= t_turn_phase) {
      if (check_line_to_serve(line, right_lines) &
          transit_signal_config.phase_extension_right) {
        state = S11_EXTENDED_RIGHT;
        serve_transit_line(line);
      } else {
        state = S13_INHIBIT;
      }
      state_timer = millis();
    }
    break;

  case S11_EXTENDED_RIGHT:
    if (millis() - state_timer >= t_extended_turn_phase) {
      if (transit_signal_config.mode == universal) {
        state = S14_INHIBIT_ALL_MODE;
      } else {
        state = S13_INHIBIT;
      }
      state_timer = millis();
    }
    break;

  case S12_LEFT_RIGHT:
    if (millis() - state_timer >= t_turn_phase) {
      if (check_line_to_serve(line, left_lines) & transit_signal_config.phase_extension_left) {
        state = S5_EXTENDED_LEFT;
        serve_transit_line(line);
      } else if (check_line_to_serve(line, right_lines) &
                 transit_signal_config.phase_extension_right) {
        state = S11_EXTENDED_RIGHT;
        serve_transit_line(line);
      } else {
        state = S13_INHIBIT;
      }
      state_timer = millis();
    }
    break;

  case S13_INHIBIT:
    if (millis() - state_timer >= t_inhibit) {
      state = S1_WAIT;
      state_timer = millis();
    }
    break;

  case S14_INHIBIT_ALL_MODE:
    if (millis() - state_timer >= t_inhibit_all_mode) {
      if (Start == 0) {
        state = S0_IDLE;
      } else {
        state = S15_LEFT_FORWARD_RIGHT;
        serve_transit_line(line);
      }
      state_timer = millis();
    }
    break;

  case S15_LEFT_FORWARD_RIGHT:
    if (millis() - state_timer >= t_all_directions_phase) {
      if (check_line_to_serve(line, left_lines) & transit_signal_config.phase_extension_left) {
        state = S5_EXTENDED_LEFT;
        serve_transit_line(line);
      } else if (check_line_to_serve(line, forward_lines) &
                 transit_signal_config.phase_extension_forward) {
        state = S8_EXTENDED_FORWARD;
        serve_transit_line(line);
      } else if (check_line_to_serve(line, right_lines) &
                 transit_signal_config.phase_extension_right) {
        state = S11_EXTENDED_RIGHT;
        serve_transit_line(line);
      } else {
        state = S14_INHIBIT_ALL_MODE;
      }
      state_timer = millis();
    }
    break;

  default:
    state = S0_IDLE;
  }
}

void do_at_leaving_hyperstate(const Line line) {
  if (check_line_to_serve(line, left_lines) &
      ((transit_signal_config.mode == forward_right_and_left) | (transit_signal_config.mode == individual))) {
    state = S4_LEFT;
    serve_transit_line(line);
  } else if ((check_line_to_serve(line, left_lines) |
              check_line_to_serve(line, forward_lines)) &
             (transit_signal_config.mode == left_forward_and_right)) {
    state = S6_LEFT_FORWARD;
    serve_transit_line(line);
  } else if (check_line_to_serve(line, forward_lines) &
             ((transit_signal_config.mode == left_right_and_forward) ||
              (transit_signal_config.mode == individual))) {
    state = S7_FORWARD;
    serve_transit_line(line);
  } else if ((check_line_to_serve(line, forward_lines) |
              check_line_to_serve(line, right_lines)) &
             (transit_signal_config.mode == forward_right_and_left)) {
    state = S9_FORWARD_RIGHT;
    serve_transit_line(line);
  } else if (check_line_to_serve(line, right_lines) &
             ((transit_signal_config.mode == left_forward_and_right) |
              (transit_signal_config.mode == individual))) {
    state = S10_RIGHT;
    serve_transit_line(line);
  } else if ((check_line_to_serve(line, left_lines) |
              check_line_to_serve(line, right_lines)) &
             (transit_signal_config.mode == left_right_and_forward)) {
    state = S12_LEFT_RIGHT;
    serve_transit_line(line);
  } else {
    state = S1_WAIT;
  }
  state_timer = millis();
}

void loop() {
  if (digitalRead(START_BTN) == LOW) {
    delay(10);
    if (digitalRead(START_BTN) == LOW) {
      Start = 1;
    }
  } else {
    Start = 0;
  }

  delay(10);

  read_transit_line();

  delay(10);

  add_line_to_queue();
 
  delay(10);

  timed_automaton_run(current_transit_line);

  delay(10);

  digitalWrite(LEFT_LED_PIN,
               (state == S1_WAIT || state == S2_BLINKING_ON ||
                state == S3_BLINKING_OFF || state == S4_LEFT ||
                state == S5_EXTENDED_LEFT || state == S6_LEFT_FORWARD ||
                state == S12_LEFT_RIGHT || state == S13_INHIBIT ||
                state == S14_INHIBIT_ALL_MODE ||
                state == S15_LEFT_FORWARD_RIGHT)
                   ? HIGH
                   : LOW);

  digitalWrite(MIDDLE_LED_PIN,
               (state == S1_WAIT || state == S2_BLINKING_ON ||
                state == S3_BLINKING_OFF || state == S6_LEFT_FORWARD ||
                state == S7_FORWARD || state == S8_EXTENDED_FORWARD ||
                state == S9_FORWARD_RIGHT || state == S13_INHIBIT ||
                state == S14_INHIBIT_ALL_MODE ||
                state == S15_LEFT_FORWARD_RIGHT)
                   ? HIGH
                   : LOW);

  digitalWrite(RIGHT_LED_PIN,
               (state == S1_WAIT || state == S2_BLINKING_ON ||
                state == S3_BLINKING_OFF || state == S9_FORWARD_RIGHT ||
                state == S10_RIGHT || state == S11_EXTENDED_RIGHT ||
                state == S12_LEFT_RIGHT || state == S13_INHIBIT ||
                state == S14_INHIBIT_ALL_MODE ||
                state == S15_LEFT_FORWARD_RIGHT)
                   ? HIGH
                   : LOW);

  digitalWrite(BOTTOM_LED_PIN,
               (state == S2_BLINKING_ON || state == S4_LEFT ||
                state == S5_EXTENDED_LEFT || state == S6_LEFT_FORWARD ||
                state == S7_FORWARD || state == S8_EXTENDED_FORWARD ||
                state == S9_FORWARD_RIGHT || state == S10_RIGHT ||
                state == S11_EXTENDED_RIGHT || state == S12_LEFT_RIGHT ||
                state == S15_LEFT_FORWARD_RIGHT)
                   ? HIGH
                   : LOW);

  digitalWrite(AUX_LED_PIN, state == S0_IDLE ? HIGH : LOW);

  delay(10);
}