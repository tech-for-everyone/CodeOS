//! A minimal `HashSet`-compatible set backed by a plain Vec for no_std.
//!
//! Provides the subset of the `std::collections::HashSet` API that penrose uses
//! (collect / contains / insert / remove / iter / FromIterator), keyed on
//! `Hash + Eq` instead of requiring `Ord`.

use alloc::{vec, vec::Vec};
use core::{
    hash::Hash,
    iter::{Extend, FromIterator},
};

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct HashSet<T> {
    data: Vec<T>,
}

impl<T: Hash + Eq> HashSet<T> {
    pub fn new() -> Self {
        Self { data: Vec::new() }
    }

    pub fn with_capacity(cap: usize) -> Self {
        Self {
            data: Vec::with_capacity(cap),
        }
    }

    pub fn len(&self) -> usize {
        self.data.len()
    }

    pub fn is_empty(&self) -> bool {
        self.data.is_empty()
    }

    pub fn clear(&mut self) {
        self.data.clear();
    }

    pub fn insert(&mut self, value: T) -> bool {
        if self.contains(&value) {
            false
        } else {
            self.data.push(value);
            true
        }
    }

    pub fn remove(&mut self, value: &T) -> bool {
        for (i, x) in self.data.iter().enumerate() {
            if x == value {
                self.data.swap_remove(i);
                return true;
            }
        }
        false
    }

    pub fn contains(&self, value: &T) -> bool {
        for x in &self.data {
            if x == value {
                return true;
            }
        }
        false
    }

    pub fn get(&self, value: &T) -> Option<&T> {
        for (i, x) in self.data.iter().enumerate() {
            if x == value {
                return self.data.get(i);
            }
        }
        None
    }

    pub fn iter(&self) -> Iter<'_, T> {
        Iter {
            inner: self.data.iter(),
        }
    }
}

pub struct Iter<'a, T> {
    inner: core::slice::Iter<'a, T>,
}

impl<'a, T> Iterator for Iter<'a, T> {
    type Item = &'a T;
    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next()
    }
}

impl<T: Hash + Eq> FromIterator<T> for HashSet<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        let mut s = Self::new();
        for v in iter {
            s.insert(v);
        }
        s
    }
}

impl<T: Hash + Eq> Extend<T> for HashSet<T> {
    fn extend<I: IntoIterator<Item = T>>(&mut self, iter: I) {
        for v in iter {
            self.insert(v);
        }
    }
}

impl<T> IntoIterator for HashSet<T> {
    type Item = T;
    type IntoIter = vec::IntoIter<T>;
    fn into_iter(self) -> Self::IntoIter {
        self.data.into_iter()
    }
}

impl<'a, T: Hash + Eq> IntoIterator for &'a HashSet<T> {
    type Item = &'a T;
    type IntoIter = Iter<'a, T>;
    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}