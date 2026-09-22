//! Geometry primitives
use crate::prelude::*;

/// An x,y coordinate pair
#[derive(Default, Debug, Copy, Clone, PartialEq, Eq, Hash)]
pub struct Point {
    /// An absolute x coordinate relative to the root window
    pub x: i32,
    /// An absolute y coordinate relative to the root window
    pub y: i32,
}

impl Point {
    /// The origin (0, 0)
    pub const ORIGIN: Self = Self::new(0, 0);

    /// Create a new Point.
    pub const fn new(x: i32, y: i32) -> Self {
        Self { x, y }
    }
}

impl From<(i32, i32)> for Point {
    fn from(raw: (i32, i32)) -> Self {
        let (x, y) = raw;

        Self { x, y }
    }
}

impl From<(&i32, &i32)> for Point {
    fn from(raw: (&i32, &i32)) -> Self {
        let (&x, &y) = raw;

        Self { x, y }
    }
}

// A Rect converts to its top left corner
impl From<Rect> for Point {
    fn from(r: Rect) -> Self {
        let Rect { x, y, .. } = r;

        Self { x, y }
    }
}

impl From<&Rect> for Point {
    fn from(r: &Rect) -> Self {
        let &Rect { x, y, .. } = r;

        Self { x, y }
    }
}

impl Neg for Point {
    type Output = Point;

    fn neg(self) -> Self::Output {
        Self::new(-self.x, self.y)
    }
}

impl Add<Point> for Point {
    type Output = Point;

    fn add(self, rhs: Point) -> Self::Output {
        Self::new(self.x + rhs.x, self.y + rhs.y)
    }
}

impl AddAssign<Point> for Point {
    fn add_assign(&mut self, rhs: Point) {
        self.x += rhs.x;
        self.y += rhs.y;
    }
}

impl Sub<Point> for Point {
    type Output = Point;

    fn sub(self, rhs: Point) -> Self::Output {
        Self::new(self.x - rhs.x, self.y - rhs.y)
    }
}

impl SubAssign<Point> for Point {
    fn sub_assign(&mut self, rhs: Point) {
        self.x -= rhs.x;
        self.y -= rhs.y;
    }
}

impl Mul<i32> for Point {
    type Output = Point;

    fn mul(self, rhs: i32) -> Self::Output {
        Self::new(self.x * rhs, self.y * rhs)
    }
}

impl MulAssign<i32> for Point {
    fn mul_assign(&mut self, rhs: i32) {
        self.x *= rhs;
        self.y *= rhs;
    }
}

impl Mul<Point> for i32 {
    type Output = Point;

    fn mul(self, rhs: Point) -> Self::Output {
        Point::new(rhs.x * self, rhs.y * self)
    }
}

impl Div<i32> for Point {
    type Output = Point;

    fn div(self, rhs: i32) -> Self::Output {
        Self::new(self.x / rhs, self.y / rhs)
    }
}

impl DivAssign<i32> for Point {
    fn div_assign(&mut self, rhs: i32) {
        self.x /= rhs;
        self.y /= rhs;
    }
}

/// An X window / screen position: top left corner + extent as percentages
/// of the current screen containing the window.
#[derive(Default, Debug, PartialEq, Clone, Copy)]
pub struct RelativeRect {
    x: f64,
    y: f64,
    w: f64,
    h: f64,
}

impl RelativeRect {
    /// Create a new RelativeRect from the provided values.
    ///
    /// Values are clamped to be in the range 0.0 to 1.0.
    pub fn new(x: f64, y: f64, w: f64, h: f64) -> Self {
        Self {
            x: x.clamp(0.0, 1.0),
            y: y.clamp(0.0, 1.0),
            w: w.clamp(0.0, 1.0),
            h: h.clamp(0.0, 1.0),
        }
    }

    /// All available space within a given Rect
    pub fn fullscreen() -> Self {
        Self {
            x: 0.0,
            y: 0.0,
            w: 1.0,
            h: 1.0,
        }
    }

    /// Apply the proportions of this RelativeRect to a given Rect.
    pub fn applied_to(&self, r: &Rect) -> Rect {
        Rect {
            x: r.x + (r.w as f64 * self.x) as i32,
            y: r.y + (r.h as f64 * self.y) as i32,
            w: (r.w as f64 * self.w) as u32,
            h: (r.h as f64 * self.h) as u32,
        }
    }

    /// Apply some [Rect] based operation to this [RelativeRect] by applying it
    /// to a given reference [Rect].
    pub fn apply_as_rect<F>(self, r: &Rect, f: F) -> Self
    where
        F: Fn(Rect) -> Rect,
    {
        f(self.applied_to(r)).relative_to(r)
    }
}

/// Something that can be converted into a [RelativeRect] by comparing to
/// some reference [Rect].
pub trait RelativeTo {
    /// Convert to a [RelativeRect] using the reference [Rect]
    fn relative_to(&self, r: &Rect) -> RelativeRect;
}

impl RelativeTo for RelativeRect {
    fn relative_to(&self, _r: &Rect) -> RelativeRect {
        *self
    }
}

// TODO: the current implemention will produce essentially garbage results if the
//       child Rect is not a subregion of the parent. This needs bounds checking
//       and some sensible default behaviour when those checks fail (such as translating
//       the Rect to fit or scaling it down)
impl RelativeTo for Rect {
    fn relative_to(&self, r: &Rect) -> RelativeRect {
        RelativeRect::new(
            (self.x.saturating_sub(r.x)) as f64 / r.w as f64,
            (self.y.saturating_sub(r.y)) as f64 / r.h as f64,
            self.w as f64 / r.w as f64,
            self.h as f64 / r.h as f64,
        )
    }
}

/// An X window / screen position: top left corner + extent
#[derive(Default, Debug, PartialEq, Eq, Clone, Copy, Hash)]
pub struct Rect {
    /// The x-coordinate of the top left corner of this rect
    pub x: i32,
    /// The y-coordinate of the top left corner of this rect
    pub y: i32,
    /// The width of this rect
    pub w: u32,
    /// The height of this rect
    pub h: u32,
}

impl From<(Point, Point)> for Rect {
    fn from((p1, p2): (Point, Point)) -> Self {
        let (x1, x2) = (min(p1.x, p2.x), max(p1.x, p2.x));
        let (y1, y2) = (min(p1.y, p2.y), max(p1.y, p2.y));

        Rect::new(x1, y1, (x2 - x1) as u32, (y2 - y1) as u32)
    }
}

impl Rect {
    /// Create a new Rect.
    pub const fn new(x: i32, y: i32, w: u32, h: u32) -> Rect {
        Rect { x, y, w, h }
    }

    /// Set the position of this [Rect] by specifying the new location of the top left corner as a
    /// [Point].
    ///
    /// ```
    /// # use penrose::pure::geometry::{Rect, Point};
    /// let mut r = Rect::new(0, 0, 100, 200);
    /// r.set_position(Point::new(20, -10));
    ///
    /// assert_eq!(r, Rect::new(20, -10, 100, 200));
    /// ```
    pub const fn set_position(&mut self, Point { x, y }: Point) {
        self.x = x;
        self.y = y;
    }

    /// The four corners of this [Rect] in [Point] form returned in clockwise
    /// order from the top left corner.
    /// ```
    /// # use penrose::pure::geometry::{Rect, Point};
    /// let r = Rect::new(0, 0, 100, 200);
    /// let corners = r.corners();
    ///
    /// assert_eq!(
    ///     corners,
    ///     (
    ///         Point { x: 0, y: 0 },
    ///         Point { x: 100, y: 0 },
    ///         Point { x: 100, y: 200 },
    ///         Point { x: 0, y: 200 },
    ///     )
    /// );
    /// ```
    pub const fn corners(&self) -> (Point, Point, Point, Point) {
        let &Rect { x, y, w, h } = self;

        (
            Point { x, y },
            Point { x: x + w as i32, y },
            Point {
                x: x + w as i32,
                y: y + h as i32,
            },
            Point { x, y: y + h as i32 },
        )
    }

    /// The midpoint of this rectangle.
    ///
    /// Odd side lengths will lead to a truncated point towards the top left corner
    /// in order to maintain integer coordinates.
    /// ```
    /// # use penrose::pure::geometry::{Rect, Point};
    /// let r = Rect::new(0, 0, 100, 200);
    ///
    /// assert_eq!(r.midpoint(), Point { x: 50, y: 100 });
    /// ```
    pub const fn midpoint(&self) -> Point {
        Point {
            x: self.x + (self.w / 2) as i32,
            y: self.y + (self.h / 2) as i32,
        }
    }

    /// Shrink width and height by the given pixel border, maintaining the current x and y
    /// coordinates. The resulting `Rect` will always have a minimum width and height of 1.
    /// ```
    /// # use penrose::pure::geometry::Rect;
    /// let r = Rect::new(0, 0, 100, 200);
    ///
    /// assert_eq!(r.shrink_in(10), Rect::new(0, 0, 80, 180));
    /// assert_eq!(r.shrink_in(50), Rect::new(0, 0, 1, 100));
    /// assert_eq!(r.shrink_in(100), Rect::new(0, 0, 1, 1));
    /// ```
    pub const fn shrink_in(&self, border: u32) -> Self {
        let w = if self.w <= 2 * border {
            1
        } else {
            self.w - 2 * border
        };
        let h = if self.h <= 2 * border {
            1
        } else {
            self.h - 2 * border
        };

        Self { w, h, ..*self }
    }

    /// Create a new [Rect] with width equal to `factor` x `self.w`
    /// ```
    /// # use penrose::pure::geometry::Rect;
    /// let r = Rect::new(0, 0, 30, 40);
    ///
    /// assert_eq!(r.scale_w(1.5), Rect::new(0, 0, 45, 40));
    /// assert_eq!(r.scale_w(0.5), Rect::new(0, 0, 15, 40));
    /// ```
    pub fn scale_w(&self, factor: f64) -> Self {
        Self {
            w: (self.w as f64 * factor) as u32,
            ..*self
        }
    }

    /// Create a new [Rect] with height equal to `factor` x `self.h`
    /// ```
    /// # use penrose::pure::geometry::Rect;
    /// let r = Rect::new(0, 0, 30, 40);
    ///
    /// assert_eq!(r.scale_h(1.5), Rect::new(0, 0, 30, 60));
    /// assert_eq!(r.scale_h(0.5), Rect::new(0, 0, 30, 20));
    /// ```
    pub fn scale_h(&self, factor: f64) -> Self {
        Self {
            h: (self.h as f64 * factor) as u32,
            ..*self
        }
    }

    /// Update the width and height of this [Rect] by specified deltas.
    ///
    /// Minimum size is clamped at 1x1.
    ///
    /// # Panics
    /// This function will panic if one of the supplied deltas overflows `i32::MAX`.
    /// ```
    /// # use penrose::pure::geometry::Rect;
    /// let mut r = Rect::new(0, 0, 100, 200);
    ///
    /// r.resize(20, 30);
    /// assert_eq!(r, Rect::new(0, 0, 120, 230));
    ///
    /// r.resize(-40, -50);
    /// assert_eq!(r, Rect::new(0, 0, 80, 180));
    /// ```
    pub fn resize(&mut self, dw: i32, dh: i32) {
        self.w = max(1, (self.w as i32) + dw) as u32;
        self.h = max(1, (self.h as i32) + dh) as u32;
    }

    /// Update the position of this [Rect] by specified deltas.
    ///
    /// # Panics
    /// This function will panic if one of the supplied deltas overflows `i32::MAX`.
    /// ```
    /// # use penrose::pure::geometry::Rect;
    /// let mut r = Rect::new(0, 0, 100, 200);
    ///
    /// r.reposition(20, 30);
    /// assert_eq!(r, Rect::new(20, 30, 100, 200));
    ///
    /// r.reposition(-40, -20);
    /// assert_eq!(r, Rect::new(-20, 10, 100, 200));
    /// ```
    pub const fn reposition(&mut self, dx: i32, dy: i32) {
        self.x += dx;
        self.y += dy;
    }

    /// Check whether this Rect contains `other` as a sub-Rect
    pub const fn contains(&self, other: &Rect) -> bool {
        match other {
            Rect { x, .. } if *x < self.x => false,
            Rect { x, w, .. } if (*x + *w as i32) > (self.x + self.w as i32) => false,
            Rect { y, .. } if *y < self.y => false,
            Rect { y, h, .. } if (*y + *h as i32) > (self.y + self.h as i32) => false,
            _ => true,
        }
    }

    /// Check whether this Rect is physically larger than `other` regardless
    /// of position.
    pub const fn is_larger_than(&self, other: &Rect) -> bool {
        self.w > other.w && self.h > other.h
    }

    /// Check whether this Rect contains `p`
    pub fn contains_point<P>(&self, p: P) -> bool
    where
        P: Into<Point>,
    {
        let p = p.into();

        (self.x..(self.x + self.w as i32 + 1)).contains(&p.x)
            && (self.y..(self.y + self.h as i32 + 1)).contains(&p.y)
    }

    /// Center this Rect inside of `enclosing`.
    ///
    /// Returns `None` if this Rect can not fit inside enclosing
    pub const fn centered_in(&self, enclosing: &Rect) -> Option<Self> {
        if self.w > enclosing.w || self.h > enclosing.h {
            return None;
        }

        Some(Self {
            x: enclosing.x + ((enclosing.w - self.w) / 2) as i32,
            y: enclosing.y + ((enclosing.h - self.h) / 2) as i32,
            ..*self
        })
    }

    /// Split this `Rect` into evenly sized rows.
    pub fn as_rows(&self, n_rows: u32) -> Vec<Rect> {
        if n_rows <= 1 {
            return vec![*self];
        }
        let h = self.h / n_rows;
        (0..n_rows)
            .map(|n| Rect::new(self.x, self.y + (n * h) as i32, self.w, h))
            .collect()
    }

    /// Split this `Rect` into evenly sized columns.
    pub fn as_columns(&self, n_columns: u32) -> Vec<Rect> {
        if n_columns <= 1 {
            return vec![*self];
        }
        let w = self.w / n_columns;
        (0..n_columns)
            .map(|n| Rect::new(self.x + (n * w) as i32, self.y, w, self.h))
            .collect()
    }

    /// Divides this rect into two columns where the first has the given width.
    ///
    /// Returns `None` if new_width is out of bounds
    pub const fn split_at_width(&self, new_width: u32) -> Option<(Self, Self)> {
        if new_width >= self.w {
            None
        } else {
            Some((
                Self {
                    w: new_width,
                    ..*self
                },
                Self {
                    x: self.x + new_width as i32,
                    w: self.w - new_width,
                    ..*self
                },
            ))
        }
    }

    /// Divides this rect into two rows where the first has the given height.
    ///
    /// Returns `None` if new_height is out of bounds
    pub const fn split_at_height(&self, new_height: u32) -> Option<(Self, Self)> {
        if new_height >= self.h {
            None
        } else {
            Some((
                Self {
                    h: new_height,
                    ..*self
                },
                Self {
                    y: self.y + new_height as i32,
                    h: self.h - new_height,
                    ..*self
                },
            ))
        }
    }

    /// Divide this rect into two columns where the first takes up `perc%` of the
    /// current width.
    ///
    /// Returns `None` if perc is not between 0.0 and 1.0
    pub fn split_at_width_perc(&self, perc: f32) -> Option<(Self, Self)> {
        if !(0.0..=1.0).contains(&perc) {
            None
        } else {
            let w = (self.w as f32 * perc) as u32;
            Some((
                Self { w, ..*self },
                Self {
                    x: self.x + w as i32,
                    w: self.w - w,
                    ..*self
                },
            ))
        }
    }

    /// Divide this rect into two rows where the first takes up `perc%` of the
    /// current height.
    ///
    /// Returns `None` if perc is not between 0.0 and 1.0
    pub fn split_at_height_perc(&self, perc: f32) -> Option<(Self, Self)> {
        if !(0.0..=1.0).contains(&perc) {
            None
        } else {
            let h = (self.h as f32 * perc) as u32;
            Some((
                Self { h, ..*self },
                Self {
                    y: self.y + h as i32,
                    h: self.h - h,
                    ..*self
                },
            ))
        }
    }

    /// Divides this rect into two columns along its midpoint.
    pub const fn split_at_mid_width(&self) -> (Self, Self) {
        let new_width = self.w / 2;
        (
            Self {
                w: new_width,
                ..*self
            },
            Self {
                x: self.x + new_width as i32,
                w: self.w - new_width,
                ..*self
            },
        )
    }

    /// Divides this rect into two rows along its midpoint.
    pub const fn split_at_mid_height(&self) -> (Self, Self) {
        let new_height = self.h / 2;
        (
            Self {
                h: new_height,
                ..*self
            },
            Self {
                y: self.y + new_height as i32,
                h: self.h - new_height,
                ..*self
            },
        )
    }
}

impl Add<Rect> for Point {
    type Output = Rect;

    fn add(self, mut rhs: Rect) -> Self::Output {
        rhs.x += self.x;
        rhs.y += self.y;

        rhs
    }
}

impl Add<Point> for Rect {
    type Output = Rect;

    fn add(mut self, rhs: Point) -> Self::Output {
        self.x += rhs.x;
        self.y += rhs.y;

        self
    }
}

impl AddAssign<Point> for Rect {
    fn add_assign(&mut self, rhs: Point) {
        self.x += rhs.x;
        self.y += rhs.y;
    }
}

