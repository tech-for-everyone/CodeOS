use std::{any::{Any, TypeId}, collections::HashMap};

pub mod any {
    pub use std::any::Any;
}

#[derive(Debug, Default)]
pub struct Map {
    values: HashMap<TypeId, Box<dyn Any>>,
}

pub type AnyMap = Map;

impl Map {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn get<T: Any>(&self) -> Option<&T> {
        self.values.get(&TypeId::of::<T>())?.downcast_ref()
    }

    pub fn get_mut<T: Any>(&mut self) -> Option<&mut T> {
        self.values.get_mut(&TypeId::of::<T>())?.downcast_mut()
    }

    pub fn insert<T: Any>(&mut self, value: T) -> Option<T> {
        self.values
            .insert(TypeId::of::<T>(), Box::new(value))
            .and_then(|previous| previous.downcast().ok().map(|value| *value))
    }

    pub fn remove<T: Any>(&mut self) -> Option<T> {
        self.values
            .remove(&TypeId::of::<T>())
            .and_then(|value| value.downcast().ok().map(|value| *value))
    }

    pub fn contains<T: Any>(&self) -> bool {
        self.values.contains_key(&TypeId::of::<T>())
    }
}
