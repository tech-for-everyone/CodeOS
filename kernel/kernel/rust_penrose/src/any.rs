//! A minimal `Any` replacement for no_std, good enough to back penrose's
//! dynamically-typed `Message`s and `State` extensions.

use alloc::boxed::Box;
use ::core::any::TypeId;

/// Minimal stand-in for `std::any::Any`.
pub trait Any: 'static {
    /// The [TypeId] of this value.
    fn type_id(&self) -> TypeId;
}

impl<T: 'static> Any for T {
    fn type_id(&self) -> TypeId {
        TypeId::of::<T>()
    }
}

impl dyn Any {
    /// Returns a reference to the concrete value if `T` is the real type.
    pub fn downcast_ref<T: 'static>(&self) -> Option<&T> {
        if self.type_id() == TypeId::of::<T>() {
            // SAFETY: the TypeId check guarantees the concrete type matches T
            Some(unsafe { &*(self as *const dyn Any as *const T) })
        } else {
            None
        }
    }
}

/// Try to downcast a boxed `dyn Any` back to its concrete `Box<T>`.
pub fn downcast_box<T: 'static>(b: Box<dyn Any>) -> Result<Box<T>, Box<dyn Any>> {
    if b.type_id() == TypeId::of::<T>() {
        // SAFETY: the TypeId check guarantees the concrete type inside the box is T
        Ok(unsafe { Box::from_raw(Box::into_raw(b) as *mut T) })
    } else {
        Err(b)
    }
}