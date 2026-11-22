// AirWatch Case v4.3 - 2-Part All-PA6-CF Print (0.16 mm, Zero Supports)
// - XIAO ESP32-S3: flat on floor (VHB on shield), 9.25 mm walls, 22 x 19.25 mm pocket, full U.FL cutout
// - ATGM336H GPS:  flat on floor (VHB on RF shield), 9.25 mm walls, 14 x 18.75 mm pocket, full U.FL cutout
// - GPS Antenna:   6.15 x 20.5 mm exposed roof pocket (6.1 mm tall antenna, nubs grip
//                  at 6.0 mm), left-side ramp notch + bottom U.FL/coax pass-through
// - 0.91" SSD1306 OLED: 3-sided C-frame (12.2 mm inner height, header stop on -X, open +X ribbon side)
// - 12 mm buzzer (7 mm deep ring, 12.5 mm ID) & 9.5 mm WS2812B LED (3.4 mm deep, 10.4 mm ID slotted ring)
// - 6x6 2-pin tact switch near rear top edge (glued, easy rear soldering) + SMA bulkhead
//
// Print orientations (pre-rotated):
//   part = "shell" -> front face down, zero supports (PA6-CF)
//   part = "lid"   -> outside down, zero supports (PA6-CF)
// Preview:
//   part = "assembly"

part = "lid";

// ---------------- general ----------------
wall      = 2.0;    // side / top / bottom walls
front     = 2.0;    // driver-facing front wall
lid_t     = 3.0;    // back lid thickness
fit       = 0.25;   // lid lip & buzzer clearance
corner_r  = 3.0;    // outer edge rounding
layer_h   = 0.16;   // slicer layer height (clean counterbore bridges)
usb_right = true;   // true = XIAO on right / GPS on left (aligns both U.FL cutouts)

// Inner cavity (outer = 70 x 50 x 41 mm)
W = 66;             // width  (left-right)
H = 46;             // height (bottom-top)
D = 36;             // depth  (front to lid)

// ---------------- floor pockets (XIAO & GPS, flat on z = 0 with VHB) ----------------
pocket_h  = 9.25;   // tall walls cup past PCB & header bases
pocket_t  = 1.8;    // pocket wall thickness

// XIAO ESP32-S3 (21 mm PCB / 23 mm w/ USB overhang, 18 mm wide, 5.5 mm PCB height, 21 mm w/ DuPonts)
xiao_l_clr  = 22.0; // inner X length (2 mm USB overhang seats into 2 mm side wall slot)
xiao_w_clr  = 19.25;// inner Y width (+1.25 mm over 18 mm board for splayed jumper wires)
xiao_cut_y  = 8.5;  // full floor-to-top inner-wall cutout at front U.FL corner
usb_open_w  = 14.5; // generous USB-C cable opening width (stays clear of rear pocket wall)
usb_open_z1 = 8.5;  // top of USB-C opening (bottom goes flush to -wall as U-slot)

// ATGM336H GPS (13 x 16 mm board: flat RF shield down on VHB, 5-pin DuPonts up at rear,
// U.FL connector on top at front-inner corner exiting through full wall cutout)
gps_w_clr = 14.0;   // inner X width
gps_l_clr = 18.75;  // inner Y length (extra room along U.FL-to-header axis)
gps_cut_y = 7.5;    // full floor-to-top inner-wall cutout at front-inner U.FL corner

// ---------------- built-in exposed GPS ceramic antenna pocket ----------------
// 20 x 6 x 6.1 mm ceramic antenna; coax exits front-left (+X) notch into bottom pass-through
gant_in_w    = 6.15; // pocket width (bump to 6.3 if you want more pronounced nubs)
gant_in_l    = 20.5; // pocket length
gant_h       = 6.1;  // antenna height/depth
gant_proud   = 0.5;  // ceramic height proud above outer roof (set 0 for totally flush)
gant_grip    = 6.0;  // spacing between opposing friction nubs
gant_shelf_t = 1.4;  // bottom shelf floor thickness
gant_wall    = 1.4;  // trough side/rear wall thickness
gant_pass_w  = 4.5;  // bottom pass-through width (flush to +X wall, clears U.FL)
gant_pass_l  = 6.5;  // bottom pass-through length
gant_notch_w = 2.8;  // coax notch width through left (+X) side of front 45-deg ramp

gant_drop = gant_h - gant_proud - wall;           // 3.6 mm below inner ceiling (5.6 mm deep)
gant_nub  = max(0, (gant_in_w - gant_grip) / 2);  // 0.075 mm per side -> 6.0 mm grip

// ---------------- front & top cutouts ----------------
// 0.91" SSD1306 OLED (38 x 12 mm PCB, 22.4 x 5.6 mm active area)
// Butt header edge (-X) against stop wall; +X ribbon end is completely open
oled_w       = 38.0;
oled_h_clr   = 12.2; // tight 12.2 mm inner height for 12 mm PCB
oled_frame_t = 1.6;
oled_frame_d = 3.5;  // cradles VHB + OLED module
oled_win_w   = 25.0;
oled_win_h   = 7.6;
oled_cx      = W / 2 + 0.4; // aligns active pixels dead-center in W/2 window
oled_cz      = H - 9.2;     // 36.8 mm up -> 29.1 mm clear below frame (8.1 mm over XIAO DuPonts)

// Active buzzer (12 x 9.5 mm) — 12.5 mm ID, deeper 7 mm cradle
buz_d      = 12.0;
buz_clr_d  = buz_d + 2 * fit; // 12.5 mm
buz_ring_h = 7.0;

// WS2812B LED (9.5 mm round board) — 10.4 mm ID, deeper 3.4 mm wire-slotted cradle
led_d      = 9.5;
led_clr_d  = 10.4;
led_ring_h = 3.4;
led_hole   = 4.8;

// Top: 6x6 2-pin tact switch (near rear opening) & SMA bulkhead
btn_body   = 6.2;
btn_pitch  = 5.0;
btn_leg_d  = 1.4;
btn_seat_d = 0.6;
sma_hole   = 6.7;

// Lid: 3x M3 screws into heat-set inserts (Ruthex RX-M3x5.7)
insert_d     = 4.0;
insert_hole  = 7.5;
boss         = 8.5;
boss_len     = 9.0;
screw_clear  = 3.4;
screw_head   = 6.0;
screw_head_h = 1.8;
lip_h        = 1.5;
lip_w        = 1.2;

$fn = 48;

// ---------------- layout (inner coords: origin = front-bottom-left) ----------------
function mx(x) = usb_right ? x : W - x;

xiao_x0    = usb_right ? W - xiao_l_clr : 0;
xiao_wallx = usb_right ? xiao_x0 - pocket_t : xiao_l_clr;
xiao_cy    = xiao_w_clr / 2;

gps_x0     = usb_right ? 0 : W - gps_w_clr;
gps_wallx  = usb_right ? gps_w_clr : W - gps_w_clr - pocket_t;

buz_c   = [mx(22.5), 17.0];             // clears 9.25 mm pocket walls & GPS DuPonts
led_c   = [mx(37.5), 17.0];             // aligned with buzzer, clear of buzzer ring & XIAO DuPonts
gant_x0 = (W - gant_in_w) / 2;          // centered on top
gant_y0 = 8.8;                          // 45-deg front ramp starts at y=3.8 (clears OLED frame)
btn_c   = [mx(16.0), D - 6.5];          // 6.5 mm from open rear edge for easy soldering
sma_c   = [mx(W - 15.0), D - 8.5];      // clears OLED, GPS pocket & top-right boss
screws  = [
  [boss / 2, H - boss / 2],
  [W - boss / 2, H - boss / 2],
  [mx(29.0), boss / 2]                  // centered in 26.4 mm open gap between GPS & XIAO
];

// ---------------- helpers ----------------
module rounded_box(x0, x1, y0, y1, z0, z1, r) {
  hull() for (x = [x0 + r, x1 - r], z = [z0 + r, z1 - r])
    translate([x, y0, z]) rotate([-90, 0, 0]) cylinder(r = r, h = y1 - y0);
}

// Round hole along z with 45-deg teardrop peak at +y (prints clean front-down)
module tear_z(d, h) {
  linear_extrude(h) hull() {
    circle(d = d);
    translate([0, d / 2 * 1.414]) circle(d = 0.01);
  }
}

// Shallow outside-top pocket with 45-deg sloped +y edge (zero overhang)
module sloped_top_pocket(x0, y0, w, l, depth) {
  hull() {
    translate([x0, y0, H + wall - depth]) cube([w, l, 0.01]);
    translate([x0, y0, H + wall]) cube([w, l + depth, 1]);
  }
}

// M3 heat-set boss with 45-deg self-supporting front ramp
module insert_boss(c) {
  x0 = max(0, min(W - boss, c[0] - boss / 2));
  z0 = max(0, min(H - boss, c[1] - boss / 2));
  difference() {
    hull() {
      translate([x0, D - boss_len, z0]) cube([boss, boss_len, boss]);
      translate([x0, D - boss_len - boss, c[1] > H / 2 ? H - 0.01 : 0])
        cube([boss, 0.01, 0.01]);
    }
    translate([c[0], D - insert_hole, c[1]])
      rotate([-90, 0, 0]) cylinder(d = insert_d, h = insert_hole + 1);
  }
}

// Inner side wall of a floor pocket: completely open from y = 0..cut_y for U.FL coax,
// then ramps up at 45 deg (zero overhang when printed front-down) to join the rear wall.
module ramped_side_wall(x, cut_y, y_end) {
  hull() {
    translate([x, cut_y, 0]) cube([pocket_t, 0.01, 0.01]);
    translate([x, cut_y + pocket_h, 0]) cube([pocket_t, 0.01, pocket_h]);
    translate([x, y_end - 0.01, 0]) cube([pocket_t, 0.01, pocket_h]);
  }
}

// Oversized USB-C U-slot through side wall with 45-deg chamfered +y ceiling
module usb_slot() {
  y0 = xiao_cy - usb_open_w / 2;
  y1 = xiao_cy + usb_open_w / 2;
  z0 = -wall;
  z1 = usb_open_z1;
  c  = 2.3; // keeps chamfered peak clear of XIAO rear pocket wall
  translate([usb_right ? W - 0.5 : -wall - 0.5, 0, 0])
    rotate([90, 0, 90])
      linear_extrude(wall + 1)
        polygon([
          [y0, z0 - 1], [y1, z0 - 1], [y1, z0],
          [y1 + c, z0 + c], [y1 + c, z1 - c], [y1, z1], [y0, z1]
        ]);
}

// Self-supporting diamond friction nub inside the GPS roof pocket (zero overhang)
module gps_nub(x_wall, dir, y_mid) {
  if (gant_nub > 0) {
    z0 = H - gant_drop;
    zh = gant_drop + wall - 0.8; // 0.8 mm open lead-in at outer roof
    bx = dir > 0 ? x_wall - 0.3 : x_wall;
    tx = dir > 0 ? x_wall + gant_nub - 0.01 : x_wall - gant_nub;
    hull() {
      translate([bx, y_mid - 1.0, z0]) cube([0.3, 2.0, zh]);
      translate([tx, y_mid - 0.3, z0]) cube([0.01, 0.6, zh]);
    }
  }
}

// ---------------- shell ----------------
module shell() {
  total_drop = gant_drop + gant_shelf_t; // 5.0 mm below inner ceiling
  pass_x0    = gant_x0 + gant_in_w - gant_pass_w;
  notch_x0   = gant_x0 + gant_in_w - gant_notch_w;

  union() {
    difference() {
      union() {
        difference() {
          rounded_box(-wall, W + wall, -front, D, -wall, H + wall, corner_r);
          cube([W, D + 1, H]);
        }

        // --- Full-length GPS antenna roof trough (45-deg front ramp = zero supports) ---
        hull() {
          translate([gant_x0 - gant_wall, gant_y0 - total_drop, H - 0.01])
            cube([gant_in_w + 2 * gant_wall, 0.01, 0.02]);
          translate([gant_x0 - gant_wall, gant_y0, H - total_drop])
            cube([gant_in_w + 2 * gant_wall, gant_in_l + gant_wall, total_drop + 0.01]);
        }

        // --- 0.91" OLED 3-sided C-frame (12.2 mm inner height, open +X ribbon side) ---
        oled_x_stop = oled_cx - oled_w / 2 - 0.3;
        oled_rail_l = oled_w + 0.3 + oled_frame_t;
        translate([oled_x_stop - oled_frame_t, 0, oled_cz - oled_h_clr / 2 - oled_frame_t])
          cube([oled_frame_t, oled_frame_d, oled_h_clr + 2 * oled_frame_t]);
        for (z = [oled_cz - oled_h_clr / 2 - oled_frame_t, oled_cz + oled_h_clr / 2])
          translate([oled_x_stop - oled_frame_t, 0, z])
            cube([oled_rail_l, oled_frame_d, oled_frame_t]);

        // --- XIAO ESP32-S3 walled floor pocket (9.25 mm walls, full U.FL cutout) ---
        ramped_side_wall(xiao_wallx, xiao_cut_y, xiao_w_clr + pocket_t);
        translate([usb_right ? xiao_wallx : 0, xiao_w_clr, 0])
          cube([xiao_l_clr + pocket_t, pocket_t, pocket_h]);

        // --- ATGM336H GPS walled floor pocket (14 x 18.75 mm, 9.25 mm walls, full U.FL cutout) ---
        ramped_side_wall(gps_wallx, gps_cut_y, gps_l_clr + pocket_t);
        translate([usb_right ? 0 : gps_wallx, gps_l_clr, 0])
          cube([gps_w_clr + pocket_t, pocket_t, pocket_h]);

        // Deeper glue-in ring for 12 mm buzzer (12.5 mm ID, 7 mm deep)
        translate([buz_c[0], 0, buz_c[1]]) rotate([-90, 0, 0]) difference() {
          cylinder(d = buz_clr_d + 2.4, h = buz_ring_h);
          translate([0, 0, -1]) cylinder(d = buz_clr_d, h = buz_ring_h + 2);
        }

        // Deeper glue-in ring for 9.5 mm WS2812B LED (10.4 mm ID, 3.4 mm deep, wire-slotted)
        translate([led_c[0], 0, led_c[1]]) rotate([-90, 0, 0]) difference() {
          cylinder(d = led_clr_d + 2.2, h = led_ring_h);
          translate([0, 0, -1]) cylinder(d = led_clr_d, h = led_ring_h + 2);
          translate([-2.5, -led_clr_d, 0.8]) cube([5.0, 2 * led_clr_d, led_ring_h + 1]);
        }

        // 3x lid screw bosses
        for (c = screws) insert_boss(c);
      }

      // Exposed GPS antenna pocket (6.15 x 20.5 mm, 5.6 mm deep from outer roof)
      translate([gant_x0, gant_y0, H - gant_drop])
        cube([gant_in_w, gant_in_l, gant_drop + wall + 1]);
      // Bottom U.FL + coax pass-through hole flush to +X inner wall (leaves 1.65 mm right ledge)
      translate([pass_x0, gant_y0, H - total_drop - 1])
        cube([gant_pass_w, gant_pass_l, gant_shelf_t + 2]);
      // Coax notch through left (+X) side of front ramp (leaves 45-deg ramps on both sides)
      translate([notch_x0, gant_y0 - total_drop - 0.1, H - total_drop - 1])
        cube([gant_notch_w, total_drop + 0.2, total_drop + 1]);

      // OLED window (45-deg exterior chamfer, centered on front face)
      translate([W / 2, -front - 0.01, oled_cz])
        hull() {
          translate([0, front, 0]) cube([oled_win_w, 0.02, oled_win_h], center = true);
          cube([oled_win_w + 1.4, 0.02, oled_win_h + 1.4], center = true);
        }

      // Buzzer acoustic holes (7x)
      for (a = [0 : 60 : 300])
        translate([buz_c[0] + 3.2 * cos(a), -front - 1, buz_c[1] + 3.2 * sin(a)])
          rotate([-90, 0, 0]) cylinder(d = 1.6, h = front + 2);
      translate([buz_c[0], -front - 1, buz_c[1]])
        rotate([-90, 0, 0]) cylinder(d = 1.9, h = front + 2);

      // LED hole
      translate([led_c[0], -front - 1, led_c[1]])
        rotate([-90, 0, 0]) cylinder(d = led_hole, h = front + 2);

      // Oversized USB-C U-slot
      usb_slot();

      // SMA antenna hole (teardrop)
      translate([sma_c[0], sma_c[1], H - 1]) tear_z(sma_hole, wall + 2);

      // 6x6 tact button leg holes + shallow glue seat (near rear edge)
      for (s = [-1, 1])
        translate([btn_c[0] + s * btn_pitch / 2, btn_c[1], H - 1])
          tear_z(btn_leg_d, wall + 2);
      sloped_top_pocket(btn_c[0] - btn_body / 2 - fit, btn_c[1] - btn_body / 2 - fit,
                        btn_body + 2 * fit, btn_body + 2 * fit, btn_seat_d);
    }

    // Light press-fit nubs gripping at 6.0 mm (added after pocket cutout)
    for (ym = [gant_y0 + 5.5, gant_y0 + gant_in_l - 5.5]) {
      gps_nub(gant_x0, +1, ym);
      gps_nub(gant_x0 + gant_in_w, -1, ym);
    }
  }
}

// ---------------- lid ----------------
module lid() {
  difference() {
    union() {
      rounded_box(-wall, W + wall, D, D + lid_t, -wall, H + wall, corner_r);
      // Alignment lip, cleared around the 3 screw bosses
      difference() {
        translate([fit, D - lip_h, fit]) cube([W - 2 * fit, lip_h, H - 2 * fit]);
        translate([fit + lip_w, D - lip_h - 1, fit + lip_w])
          cube([W - 2 * fit - 2 * lip_w, lip_h + 2, H - 2 * fit - 2 * lip_w]);
        for (c = screws)
          translate([max(0, min(W - boss, c[0] - boss / 2)) - fit,
                     D - lip_h - 1,
                     max(0, min(H - boss, c[1] - boss / 2)) - fit])
            cube([boss + 2 * fit, lip_h + 2, boss + 2 * fit]);
      }
    }

    // M3 counterbores with 2-layer clean-bridge trick (0.16 mm layers)
    for (c = screws) {
      translate([c[0], D - 1, c[1]])
        rotate([-90, 0, 0]) cylinder(d = screw_clear, h = lid_t + 2);
      translate([c[0], D + lid_t - screw_head_h, c[1]])
        rotate([-90, 0, 0]) cylinder(d = screw_head, h = screw_head_h + 1);
      cb = D + lid_t - screw_head_h;
      translate([c[0] - screw_head / 2, cb - layer_h, c[1] - screw_clear / 2])
        cube([screw_head, layer_h + 0.01, screw_clear]);
      translate([c[0] - screw_clear / 2, cb - 2 * layer_h, c[1] - screw_clear / 2])
        cube([screw_clear, 2 * layer_h + 0.01, screw_clear]);
    }
  }
}

// ---------------- preview stand-ins ----------------
module preview_parts() {
  // XIAO upside down on floor VHB: 21 mm PCB + 2 mm USB overhang (=23 mm), 18 mm wide,
  // 5.5 mm to top of PCB (w/ VHB + shield), 21 mm total height w/ DuPonts plugged in
  xx = usb_right ? W - 21.3 : 0.3;
  xy = 0.6;
  xz = 4.3; // top of 1.2 mm PCB sits at 5.5 mm
  color("Gold")      translate([xx + 5, xy + 2.5, 1.1]) cube([11, 13.0, 3.2]);
  color("Silver")    translate([usb_right ? xx + 15.0 : xx - 2.0, xiao_cy - 4.5, xz - 3.2]) cube([8.0, 9.0, 3.2]);
  color("SteelBlue") translate([xx, xy, xz]) cube([21.0, 18.0, 1.2]);
  for (y = [xy, xy + 18.0 - 2.54])
    color("Black") translate([xx + 1.5, y, xz + 1.2]) cube([17.8, 2.54, 15.5]);
  color("Gold")  translate([usb_right ? xx + 2.0 : xx + 19.0, xy + 2.2, xz - 2.3]) cylinder(d = 2.6, h = 2.3);
  color("Black") translate([usb_right ? xx - 10 : xx + 19.0, xy + 1.6, xz - 2.0]) cube([12, 1.2, 1.2]);

  // ATGM336H GPS right-side up on floor VHB (flat RF shield down; DuPonts, battery & U.FL on top)
  gx = usb_right ? 0.5 : W - 13.5;
  gy = 1.2;
  gz = 1.1 + 2.3;
  color("Silver")      translate([gx + 1.5, gy + 2.0, 1.1]) cube([10.0, 10.0, 2.3]);
  color("ForestGreen") translate([gx, gy, gz]) cube([13.0, 16.0, 1.2]);
  color("Gold")        translate([gx + 0.15, gy + 13.4, gz + 1.2]) cube([12.7, 2.54, 2.5]);
  color("Black")       translate([gx + 0.15, gy + 13.4, gz + 3.7]) cube([7.7, 2.54, 13.0]);
  color("Silver")      translate([gx + 7.5, gy + 6.5, gz + 1.2]) cylinder(d = 5.5, h = 2.4);
  color("Goldenrod")   translate([usb_right ? gx + 10.5 : gx + 2.5, gy + 2.8, gz + 1.2]) cylinder(d = 2.6, h = 2.3);
  color("DimGray")     translate([usb_right ? gx + 10.5 : gx - 1.5, gy + 2.2, gz + 2.2]) cube([12, 1.2, 1.2]);

  // OLED seated in C-frame on 1.1 mm side VHB strips (header on -X, off-center ribbon on +X)
  color("RoyalBlue") translate([oled_cx - 19.0, 1.1, oled_cz - 6.0]) cube([38.0, 2.7, 12.0]);
  color("Black")     translate([oled_cx - 18.1, 3.8, oled_cz - 5.1]) cube([2.6, 14.0, 10.2]);
  color("Orange")    translate([oled_cx + 18.8, 1.1, oled_cz - 5.4]) cube([1.7, 2.7, 8.8]);

  // Buzzer & LED
  color("DimGray")   translate([buz_c[0], 0, buz_c[1]]) rotate([-90, 0, 0]) cylinder(d = buz_d, h = 9.5);
  color("White")     translate([led_c[0], 0, led_c[1]]) rotate([-90, 0, 0]) cylinder(d = led_d, h = 3.0);

  // Exposed 20 x 6 x 6.1 mm ceramic GPS antenna; coax exits front-left (+X) notch & curves down
  ant_y0 = gant_y0 + (gant_in_l - 20.0) / 2;
  ant_z0 = H - gant_drop;
  coax_x = W / 2 + 1.6;
  color("ForestGreen") translate([W / 2 - 3.0, ant_y0, ant_z0]) cube([6.0, 20.0, 1.0]);
  color("PeachPuff")   translate([W / 2 - 3.0, ant_y0, ant_z0 + 1.0]) cube([6.0, 20.0, gant_h - 1.0]);
  color("DimGray")     translate([coax_x, ant_y0 - 2.2, ant_z0 + 0.5]) rotate([-90, 0, 0]) cylinder(d = 1.25, h = 2.5);
  color("DimGray")     translate([coax_x, ant_y0 - 2.2, ant_z0 - 5.5]) cylinder(d = 1.25, h = 6.0);

  // SMA & rear-top button
  color("Goldenrod") translate([sma_c[0], sma_c[1], H - 10]) cylinder(d = 6.35, h = 18);
  color("Black")     translate([btn_c[0] - 3.1, btn_c[1] - 3.1, H + wall - btn_seat_d]) cube([6.2, 6.2, 3.5]);
}

// ---------------- output ----------------
if (part == "shell")
  translate([0, 0, front]) rotate([90, 0, 0]) shell();
else if (part == "lid")
  translate([0, 0, D + lid_t]) rotate([-90, 0, 0]) lid();
else if (part == "assembly") {
  color("DarkSlateGray", 0.35) shell();
  color("DarkSlateGray", 0.35) translate([0, 25, 0]) lid();
  preview_parts();
}