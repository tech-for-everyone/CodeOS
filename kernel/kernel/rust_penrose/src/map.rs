//! A minimal `HashMap`-compatible map backed by a plain Vec for no_std.
//!
//! Provides the subset of the `std::collections::HashMap` API that penrose uses,
//! keyed on `Eq` (linear scan) instead of requiring `Ord` or a hasher.

use alloc::vec::Vec;
use core::{
    fmt,
    iter::{Extend, FromIterator},
};

#[derive(Clone, Default)]
pub struct HashMap<K, V> {
    data: Vec<(K, V)>,
}

impl<K: Eq, V> HashMap<K, V> {
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

    pub fn contains_key(&self, key: &K) -> bool {
        self.data.iter().any(|(k, _)| k == key)
    }

    pub fn get<Q: ?Sized>(&self, key: &Q) -> Option<&V>
    where
        K: PartialEq<Q>,
    {
        self.data
            .iter()
            .find(|(k, _)| k == key)
            .map(|(_, v)| v)
    }

    pub fn get_mut<Q: ?Sized>(&mut self, key: &Q) -> Option<&mut V>
    where
        K: PartialEq<Q>,
    {
        self.data
            .iter_mut()
            .find(|(k, _)| k == key)
            .map(|(_, v)| v)
    }

    pub fn insert(&mut self, key: K, value: V) -> Option<V> {
        if let Some(slot) = self.data.iter_mut().find(|(k, _)| *k == key) {
            Some(core::mem::replace(&mut slot.1, value))
        } else {
            self.data.push((key, value));
            None
        }
    }

    pub fn remove<Q: ?Sized>(&mut self, key: &Q) -> Option<V>
    where
        K: PartialEq<Q>,
    {
        match self.data.iter().position(|(k, _)| k == key) {
            Some(i) => Some(self.data.swap_remove(i).1),
            None => None,
        }
    }

    pub fn iter(&self) -> Iter<'_, K, V> {
        Iter {
            inner: self.data.iter(),
        }
    }

    pub fn iter_mut(&mut self) -> IterMut<'_, K, V> {
        IterMut {
            inner: self.data.iter_mut(),
        }
    }

    pub fn keys(&self) -> Keys<'_, K, V> {
        Keys {
            inner: self.data.iter(),
        }
    }

    pub fn values(&self) -> Values<'_, K, V> {
        Values {
            inner: self.data.iter(),
        }
    }

    pub fn entry(&mut self, key: K) -> Entry<'_, K, V> {
        match self.data.iter().position(|(k, _)| *k == key) {
            Some(i) => Entry::Occupied(OccupiedEntry { index: i, map: self }),
            None => Entry::Vacant(VacantEntry {
                key: Some(key),
                map: self,
            }),
        }
    }
}

impl<K: Eq, V: PartialEq> PartialEq for HashMap<K, V> {
    fn eq(&self, other: &Self) -> bool {
        self.len() == other.len()
            && self
                .data
                .iter()
                .all(|(k, v)| other.get(k) == Some(v))
    }
}

impl<K: Eq, V: Eq> Eq for HashMap<K, V> {}

impl<K: Eq + fmt::Debug, V: fmt::Debug> fmt::Debug for HashMap<K, V> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_map().entries(self.data.iter().map(|(k, v)| (k, v))).finish()
    }
}

pub struct Iter<'a, K, V> {
    inner: core::slice::Iter<'a, (K, V)>,
}

impl<'a, K, V> Iterator for Iter<'a, K, V> {
    type Item = (&'a K, &'a V);
    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next().map(|(k, v)| (k, v))
    }
}

pub struct IterMut<'a, K, V> {
    inner: core::slice::IterMut<'a, (K, V)>,
}

impl<'a, K, V> Iterator for IterMut<'a, K, V> {
    type Item = (&'a K, &'a mut V);
    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next().map(|tuple| {
            let (k, v) = tuple;
            (&*k, &mut *v)
        })
    }
}

pub struct Keys<'a, K, V> {
    inner: core::slice::Iter<'a, (K, V)>,
}

impl<'a, K, V> Iterator for Keys<'a, K, V> {
    type Item = &'a K;
    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next().map(|(k, _)| k)
    }
}

pub struct Values<'a, K, V> {
    inner: core::slice::Iter<'a, (K, V)>,
}

impl<'a, K, V> Iterator for Values<'a, K, V> {
    type Item = &'a V;
    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next().map(|(_, v)| v)
    }
}

pub enum Entry<'a, K, V> {
    Occupied(OccupiedEntry<'a, K, V>),
    Vacant(VacantEntry<'a, K, V>),
}

impl<'a, K: Eq, V> Entry<'a, K, V> {
    pub fn and_modify<F>(self, f: F) -> Self
    where
        F: FnOnce(&mut V),
    {
        match self {
            Entry::Occupied(mut o) => {
                f(o.get_mut());
                Entry::Occupied(o)
            }
            Entry::Vacant(v) => Entry::Vacant(v),
        }
    }

    pub fn or_insert(self, default: V) -> &'a mut V {
        self.or_insert_with(move || default)
    }

    pub fn or_insert_with<F>(self, default: F) -> &'a mut V
    where
        F: FnOnce() -> V,
    {
        match self {
            Entry::Occupied(o) => o.into_mut(),
            Entry::Vacant(v) => v.insert(default()),
        }
    }

    pub fn or_insert_with_key<F>(self, default: F) -> &'a mut V
    where
        F: FnOnce(&K) -> V,
    {
        match self {
            Entry::Occupied(o) => o.into_mut(),
            Entry::Vacant(v) => {
                let k = v.key();
                let val = default(k);
                v.insert(val)
            }
        }
    }

    pub fn or_default(self) -> &'a mut V
    where
        V: Default,
    {
        self.or_insert_with(V::default)
    }

    pub fn key(&self) -> &K {
        match self {
            Entry::Occupied(o) => o.key(),
            Entry::Vacant(v) => v.key.as_ref().unwrap(),
        }
    }
}

pub struct OccupiedEntry<'a, K, V> {
    index: usize,
    map: &'a mut HashMap<K, V>,
}

impl<'a, K: Eq, V> OccupiedEntry<'a, K, V> {
    pub fn get(&self) -> &V {
        &self.map.data[self.index].1
    }

    pub fn get_mut(&mut self) -> &mut V {
        &mut self.map.data[self.index].1
    }

    pub fn into_mut(self) -> &'a mut V {
        &mut self.map.data[self.index].1
    }

    pub fn key(&self) -> &K {
        &self.map.data[self.index].0
    }

    pub fn insert(&mut self, value: V) -> V {
        core::mem::replace(&mut self.map.data[self.index].1, value)
    }

    pub fn remove(self) -> V {
        self.map.data.remove(self.index).1
    }
}

pub struct VacantEntry<'a, K, V> {
    key: Option<K>,
    map: &'a mut HashMap<K, V>,
}

impl<'a, K: Eq, V> VacantEntry<'a, K, V> {
    pub fn insert(mut self, value: V) -> &'a mut V {
        let key = self.key.take().unwrap();
        self.map.data.push((key, value));
        let i = self.map.data.len() - 1;
        &mut self.map.data[i].1
    }

    pub fn key(&self) -> &K {
        self.key.as_ref().unwrap()
    }
}

impl<K, V> FromIterator<(K, V)> for HashMap<K, V>
where
    K: Eq,
{
    fn from_iter<I: IntoIterator<Item = (K, V)>>(iter: I) -> Self {
        let mut m = Self::new();
        for (k, v) in iter {
            m.insert(k, v);
        }
        m
    }
}

impl<K, V> Extend<(K, V)> for HashMap<K, V>
where
    K: Eq,
{
    fn extend<I: IntoIterator<Item = (K, V)>>(&mut self, iter: I) {
        for (k, v) in iter {
            self.insert(k, v);
        }
    }
}

pub struct IntoIter<K, V> {
    inner: alloc::vec::IntoIter<(K, V)>,
}

impl<K, V> Iterator for IntoIter<K, V> {
    type Item = (K, V);
    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next()
    }
}

impl<K, V> IntoIterator for HashMap<K, V> {
    type Item = (K, V);
    type IntoIter = IntoIter<K, V>;
    fn into_iter(self) -> Self::IntoIter {
        IntoIter {
            inner: self.data.into_iter(),
        }
    }
}

impl<'a, K: Eq, V> IntoIterator for &'a HashMap<K, V> {
    type Item = (&'a K, &'a V);
    type IntoIter = Iter<'a, K, V>;
    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<'a, K: Eq, V> IntoIterator for &'a mut HashMap<K, V> {
    type Item = (&'a K, &'a mut V);
    type IntoIter = IterMut<'a, K, V>;
    fn into_iter(self) -> Self::IntoIter {
        self.iter_mut()
    }
}